/*
 * Copyright (C) 2012-2018 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebResourceLoader.h"
#include <cstdio>
#include <dlfcn.h>

#include "FormDataReference.h"
#include "Logging.h"
#include "MessageSenderInlines.h"
#include "NetworkProcessConnection.h"
#include "NetworkResourceLoaderMessages.h"
#include "PrivateRelayed.h"
#include "SharedBufferReference.h"
#include "WebCoreArgumentCoders.h"
#include "WebErrors.h"
#include "WebFrame.h"
#include "WebLoaderStrategy.h"
#include "WebLocalFrameLoaderClient.h"
#include "WebPage.h"
#include "WebProcess.h"
#include "WebURLSchemeHandlerProxy.h"
#include <WebCore/ApplicationCacheHost.h>
#include <WebCore/CertificateInfo.h>
#include <WebCore/DiagnosticLoggingClient.h>
#include <WebCore/DiagnosticLoggingKeys.h>
#include <WebCore/DocumentLoader.h>
#include <WebCore/FrameLoader.h>
#include <WebCore/InspectorInstrumentationWebKit.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/LocalFrameLoaderClient.h>
#include <WebCore/NetworkLoadMetrics.h>
#include <WebCore/Page.h>
#include <WebCore/ResourceError.h>
#include <WebCore/ResourceLoader.h>
#include <WebCore/SubresourceLoader.h>
#include <WebCore/SubstituteData.h>
#include <wtf/CompletionHandler.h>

#define WEBRESOURCELOADER_RELEASE_LOG(fmt, ...) RELEASE_LOG(Network, "%p - [webPageID=%" PRIu64 ", frameID=%" PRIu64 ", resourceID=%" PRIu64 "] WebResourceLoader::" fmt, this, m_trackingParameters.pageID.toUInt64(), m_trackingParameters.frameID.object().toUInt64(), m_trackingParameters.resourceID.toUInt64(), ##__VA_ARGS__)

namespace WebKit {
using namespace WebCore;

Ref<WebResourceLoader> WebResourceLoader::create(Ref<ResourceLoader>&& coreLoader, const TrackingParameters& trackingParameters)
{
    return adoptRef(*new WebResourceLoader(WTFMove(coreLoader), trackingParameters));
}

WebResourceLoader::WebResourceLoader(Ref<WebCore::ResourceLoader>&& coreLoader, const TrackingParameters& trackingParameters)
    : m_coreLoader(WTFMove(coreLoader))
    , m_trackingParameters(trackingParameters)
{
}

WebResourceLoader::~WebResourceLoader()
{
    const char* url = "<null>";
    CString urlString;
    if (m_coreLoader) {
        urlString = m_coreLoader->url().string().utf8();
        if (const char* data = urlString.data())
            url = data;
    }
    fprintf(stderr, "[WRL-LIFE] destroy resourceID=%" PRIu64 " hasCoreLoader=%d bytes=%zu url=%s\n",
        m_trackingParameters.resourceID.toUInt64(),
        !!m_coreLoader,
        m_numBytesReceived,
        url);
}

IPC::Connection* WebResourceLoader::messageSenderConnection() const
{
    return &WebProcess::singleton().ensureNetworkProcessConnection().connection();
}

uint64_t WebResourceLoader::messageSenderDestinationID() const
{
    RELEASE_ASSERT(RunLoop::isMain());
    RELEASE_ASSERT(m_coreLoader->identifier());
    return m_coreLoader->identifier().toUInt64();
}

void WebResourceLoader::detachFromCoreLoader()
{
    RELEASE_ASSERT(RunLoop::isMain());
    const char* url = "<null>";
    const char* callerSymbol = "<unknown>";
    const char* callerImage = "<unknown>";
    CString urlString;
    bool isMainFrame = false;
    bool isMainResource = false;
    void* callerAddress = __builtin_return_address(0);
    uintptr_t callerOffset = 0;
    if (callerAddress) {
        Dl_info info { };
        if (dladdr(callerAddress, &info)) {
            if (info.dli_sname)
                callerSymbol = info.dli_sname;
            if (info.dli_fname)
                callerImage = info.dli_fname;
            if (info.dli_fbase)
                callerOffset = reinterpret_cast<uintptr_t>(callerAddress) - reinterpret_cast<uintptr_t>(info.dli_fbase);
        }
    }
    if (m_coreLoader) {
        urlString = m_coreLoader->url().string().utf8();
        if (const char* data = urlString.data())
            url = data;
        if (auto* frame = m_coreLoader->frame())
            isMainFrame = frame->isMainFrame();
        isMainResource = mainFrameMainResource() == MainFrameMainResource::Yes;
    }
    fprintf(stderr, "[WRL-LIFE] detach resourceID=%" PRIu64 " bytes=%zu isMainFrame=%d isMainResource=%d caller=%s image=%s addr=%p offset=0x%" PRIxPTR " url=%s\n",
        m_trackingParameters.resourceID.toUInt64(),
        m_numBytesReceived,
        isMainFrame,
        isMainResource,
        callerSymbol,
        callerImage,
        callerAddress,
        callerOffset,
        url);
    m_coreLoader = nullptr;
}

MainFrameMainResource WebResourceLoader::mainFrameMainResource() const
{
    auto* frame = m_coreLoader->frame();
    if (!frame || !frame->isMainFrame())
        return MainFrameMainResource::No;

    auto* frameLoader = m_coreLoader->frameLoader();
    if (!frameLoader)
        return MainFrameMainResource::No;

    if (!frameLoader->notifier().isInitialRequestIdentifier(m_coreLoader->identifier()))
        return MainFrameMainResource::No;

    return MainFrameMainResource::Yes;
}

void WebResourceLoader::willSendRequest(ResourceRequest&& proposedRequest, IPC::FormDataReference&& proposedRequestBody, ResourceResponse&& redirectResponse, CompletionHandler<void(ResourceRequest&&, bool)>&& completionHandler)
{
    Ref<WebResourceLoader> protectedThis(*this);

    // Make the request whole again as we do not normally encode the request's body when sending it over IPC, for performance reasons.
    proposedRequest.setHTTPBody(proposedRequestBody.takeData());

    LOG(Network, "(WebProcess) WebResourceLoader::willSendRequest to '%s'", proposedRequest.url().string().latin1().data());
    WEBRESOURCELOADER_RELEASE_LOG("willSendRequest:");

    if (m_coreLoader->documentLoader()->applicationCacheHost().maybeLoadFallbackForRedirect(m_coreLoader.get(), proposedRequest, redirectResponse)) {
        WEBRESOURCELOADER_RELEASE_LOG("willSendRequest: exiting early because maybeLoadFallbackForRedirect returned false");
        return completionHandler({ }, false);
    }
    
    if (auto* frame = m_coreLoader->frame()) {
        if (auto* page = frame->page()) {
            if (!page->allowsLoadFromURL(proposedRequest.url(), mainFrameMainResource()))
                proposedRequest = { };
        }
    }

    m_coreLoader->willSendRequest(WTFMove(proposedRequest), redirectResponse, [this, protectedThis = WTFMove(protectedThis), completionHandler = WTFMove(completionHandler)] (ResourceRequest&& request) mutable {
        if (!m_coreLoader || !m_coreLoader->identifier()) {
            WEBRESOURCELOADER_RELEASE_LOG("willSendRequest: exiting early because no coreloader or identifier");
            return completionHandler({ }, false);
        }

        WEBRESOURCELOADER_RELEASE_LOG("willSendRequest: returning ContinueWillSendRequest");
        completionHandler(WTFMove(request), m_coreLoader->isAllowedToAskUserForCredentials());
    });
}

void WebResourceLoader::didSendData(uint64_t bytesSent, uint64_t totalBytesToBeSent)
{
    m_coreLoader->didSendData(bytesSent, totalBytesToBeSent);
}

void WebResourceLoader::didReceiveResponse(ResourceResponse&& response, PrivateRelayed privateRelayed, bool needsContinueDidReceiveResponseMessage, std::optional<NetworkLoadMetrics>&& metrics)
{
    fprintf(stderr, "[WRL] didReceiveResponse: status=%d url=%s needsContinue=%d\n",
        response.httpStatusCode(),
        response.url().string().utf8().data(),
        needsContinueDidReceiveResponseMessage);
    LOG(Network, "(WebProcess) WebResourceLoader::didReceiveResponse for '%s'. Status %d.", m_coreLoader->url().string().latin1().data(), response.httpStatusCode());
    WEBRESOURCELOADER_RELEASE_LOG("didReceiveResponse: (httpStatusCode=%d)", response.httpStatusCode());

    Ref<WebResourceLoader> protectedThis(*this);

    if (metrics) {
        metrics->workerStart = m_workerStart;
        response.setDeprecatedNetworkLoadMetrics(Box<NetworkLoadMetrics>::create(WTFMove(*metrics)));
    }

    if (privateRelayed == PrivateRelayed::Yes && mainFrameMainResource() == MainFrameMainResource::Yes)
        WebProcess::singleton().setHadMainFrameMainResourcePrivateRelayed();

    if (m_coreLoader->documentLoader()->applicationCacheHost().maybeLoadFallbackForResponse(m_coreLoader.get(), response)) {
        WEBRESOURCELOADER_RELEASE_LOG("didReceiveResponse: not continuing load because the content is already cached");
        return;
    }

    CompletionHandler<void()> policyDecisionCompletionHandler;
    if (needsContinueDidReceiveResponseMessage) {
#if ASSERT_ENABLED
        m_isProcessingNetworkResponse = true;
#endif
        policyDecisionCompletionHandler = [this, protectedThis = WTFMove(protectedThis)] {
#if ASSERT_ENABLED
            m_isProcessingNetworkResponse = false;
#endif
            // If m_coreLoader becomes null as a result of the didReceiveResponse callback, we can't use the send function().
            if (m_coreLoader && m_coreLoader->identifier()) {
                fprintf(stderr, "[WRL] didReceiveResponse: sending ContinueDidReceiveResponse url=%s\n",
                    m_coreLoader->url().string().utf8().data());
                send(Messages::NetworkResourceLoader::ContinueDidReceiveResponse());
            } else
                WEBRESOURCELOADER_RELEASE_LOG("didReceiveResponse: not continuing load because no coreLoader or no ID");
        };
    }

    if (InspectorInstrumentationWebKit::shouldInterceptResponse(m_coreLoader->frame(), response)) {
        auto interceptedRequestIdentifier = m_coreLoader->identifier();
        m_interceptController.beginInterceptingResponse(interceptedRequestIdentifier);
        InspectorInstrumentationWebKit::interceptResponse(m_coreLoader->frame(), response, interceptedRequestIdentifier, [this, protectedThis = Ref { *this }, interceptedRequestIdentifier, policyDecisionCompletionHandler = WTFMove(policyDecisionCompletionHandler)](const ResourceResponse& inspectorResponse, RefPtr<FragmentedSharedBuffer> overrideData) mutable {
            if (!m_coreLoader || !m_coreLoader->identifier()) {
                fprintf(stderr, "[WRL] didReceiveResponse: intercepted callback lost coreLoader\n");
                WEBRESOURCELOADER_RELEASE_LOG("didReceiveResponse: not continuing intercept load because no coreLoader or no ID");
                m_interceptController.continueResponse(interceptedRequestIdentifier);
                return;
            }

            m_coreLoader->didReceiveResponse(inspectorResponse, [this, protectedThis = WTFMove(protectedThis), interceptedRequestIdentifier, policyDecisionCompletionHandler = WTFMove(policyDecisionCompletionHandler), overrideData = WTFMove(overrideData)]() mutable {
                if (policyDecisionCompletionHandler)
                    policyDecisionCompletionHandler();

                if (!m_coreLoader || !m_coreLoader->identifier()) {
                    m_interceptController.continueResponse(interceptedRequestIdentifier);
                    return;
                }

                RefPtr<WebCore::ResourceLoader> protectedCoreLoader = m_coreLoader;
                if (!overrideData)
                    m_interceptController.continueResponse(interceptedRequestIdentifier);
                else {
                    m_interceptController.interceptedResponse(interceptedRequestIdentifier);
                    if (unsigned bufferSize = overrideData->size())
                        protectedCoreLoader->didReceiveBuffer(overrideData.releaseNonNull(), bufferSize, DataPayloadWholeResource);
                    WebCore::NetworkLoadMetrics emptyMetrics;
                    protectedCoreLoader->didFinishLoading(emptyMetrics);
                }
            });
        });
        return;
    }

    fprintf(stderr, "[WRL] didReceiveResponse: forwarding to core loader url=%s\n",
        m_coreLoader->url().string().utf8().data());
    m_coreLoader->didReceiveResponse(response, WTFMove(policyDecisionCompletionHandler));
}

void WebResourceLoader::didReceiveData(IPC::SharedBufferReference&& data, uint64_t encodedDataLength)
{
    const char* receiveURL = "<null>";
    CString receiveURLString;
    if (m_coreLoader) {
        receiveURLString = m_coreLoader->url().string().utf8();
        if (const char* data = receiveURLString.data())
            receiveURL = data;
    }
    fprintf(stderr, "[WRL] didReceiveData: size=%zu total=%zu url=%s\n", data.size(), m_numBytesReceived + data.size(), receiveURL);
    LOG(Network, "(WebProcess) WebResourceLoader::didReceiveData of size %lu for '%s'", data.size(), m_coreLoader->url().string().latin1().data());
    ASSERT_WITH_MESSAGE(!m_isProcessingNetworkResponse, "Network process should not send data until we've validated the response");

    if (UNLIKELY(m_interceptController.isIntercepting(m_coreLoader->identifier()))) {
        m_interceptController.defer(m_coreLoader->identifier(), [this, protectedThis = Ref { *this }, buffer = WTFMove(data), encodedDataLength]() mutable {
            if (m_coreLoader)
                didReceiveData(WTFMove(buffer), encodedDataLength);
        });
        return;
    }

    if (!m_numBytesReceived)
        WEBRESOURCELOADER_RELEASE_LOG("didReceiveData: Started receiving data");
    m_numBytesReceived += data.size();

    m_coreLoader->didReceiveData(data.isNull() ? SharedBuffer::create() : data.unsafeBuffer().releaseNonNull(), encodedDataLength, DataPayloadBytes);
    const char* returnURL = "<null>";
    CString returnURLString;
    if (m_coreLoader) {
        returnURLString = m_coreLoader->url().string().utf8();
        if (const char* data = returnURLString.data())
            returnURL = data;
    }
    fprintf(stderr, "[WRL] didReceiveData: return total=%zu url=%s\n", m_numBytesReceived, returnURL);
}

void WebResourceLoader::didFinishResourceLoad(NetworkLoadMetrics&& networkLoadMetrics)
{
    const char* finishURL = "<null>";
    CString finishURLString;
    if (m_coreLoader) {
        finishURLString = m_coreLoader->url().string().utf8();
        if (const char* data = finishURLString.data())
            finishURL = data;
    }
    fprintf(stderr, "[WRL] didFinishResourceLoad: totalBytes=%zu url=%s\n", m_numBytesReceived, finishURL);
    LOG(Network, "(WebProcess) WebResourceLoader::didFinishResourceLoad for '%s'", m_coreLoader->url().string().latin1().data());
    WEBRESOURCELOADER_RELEASE_LOG("didFinishResourceLoad: (length=%zd)", m_numBytesReceived);

    if (UNLIKELY(m_interceptController.isIntercepting(m_coreLoader->identifier()))) {
        m_interceptController.defer(m_coreLoader->identifier(), [this, protectedThis = Ref { *this }, networkLoadMetrics = WTFMove(networkLoadMetrics)]() mutable {
            if (m_coreLoader)
                didFinishResourceLoad(WTFMove(networkLoadMetrics));
        });
        return;
    }

    networkLoadMetrics.workerStart = m_workerStart;

    ASSERT_WITH_MESSAGE(!m_isProcessingNetworkResponse, "Load should not be able to finish before we've validated the response");
    m_coreLoader->didFinishLoading(networkLoadMetrics);
}

void WebResourceLoader::didFailServiceWorkerLoad(const ResourceError& error)
{
    auto requestURL = m_coreLoader ? m_coreLoader->request().url().string().utf8() : CString();
    auto failingURL = error.failingURL().string().utf8();
    auto domain = error.domain().utf8();
    auto description = error.localizedDescription().utf8();
    fprintf(stderr, "[WRL-FAIL] service-worker url=%s failing=%s domain=%s code=%d desc=%s\n",
        requestURL.data() ? requestURL.data() : "<null>",
        failingURL.data() ? failingURL.data() : "<null>",
        domain.data() ? domain.data() : "<null>",
        error.errorCode(),
        description.data() ? description.data() : "<null>");
    if (auto* document = m_coreLoader->frame() ? m_coreLoader->frame()->document() : nullptr) {
        if (m_coreLoader->options().destination != FetchOptions::Destination::EmptyString || error.isGeneral())
            document->addConsoleMessage(MessageSource::JS, MessageLevel::Error, error.localizedDescription());
        if (m_coreLoader->options().destination != FetchOptions::Destination::EmptyString)
            document->addConsoleMessage(MessageSource::JS, MessageLevel::Error, makeString("Cannot load ", error.failingURL().string(), "."));
    }

    didFailResourceLoad(error);
}

void WebResourceLoader::serviceWorkerDidNotHandle()
{
#if ENABLE(SERVICE_WORKER)
    WEBRESOURCELOADER_RELEASE_LOG("serviceWorkerDidNotHandle:");

    ASSERT(m_coreLoader->options().serviceWorkersMode == ServiceWorkersMode::Only);
    auto error = internalError(m_coreLoader->request().url());
    error.setType(ResourceError::Type::Cancellation);
    m_coreLoader->didFail(error);
#else
    ASSERT_NOT_REACHED();
#endif
}

void WebResourceLoader::didFailResourceLoad(const ResourceError& error)
{
    auto requestURL = m_coreLoader ? m_coreLoader->request().url().string().utf8() : CString();
    auto failingURL = error.failingURL().string().utf8();
    auto domain = error.domain().utf8();
    auto description = error.localizedDescription().utf8();
    bool isMainFrame = m_coreLoader && m_coreLoader->frame() ? m_coreLoader->frame()->isMainFrame() : false;
    fprintf(stderr, "[WRL-FAIL] resource url=%s failing=%s domain=%s code=%d desc=%s isMainFrame=%d\n",
        requestURL.data() ? requestURL.data() : "<null>",
        failingURL.data() ? failingURL.data() : "<null>",
        domain.data() ? domain.data() : "<null>",
        error.errorCode(),
        description.data() ? description.data() : "<null>",
        isMainFrame);
    LOG(Network, "(WebProcess) WebResourceLoader::didFailResourceLoad for '%s'", m_coreLoader->url().string().latin1().data());
    WEBRESOURCELOADER_RELEASE_LOG("didFailResourceLoad:");

    if (UNLIKELY(m_interceptController.isIntercepting(m_coreLoader->identifier()))) {
        m_interceptController.defer(m_coreLoader->identifier(), [this, protectedThis = Ref { *this }, error]() mutable {
            if (m_coreLoader)
                didFailResourceLoad(error);
        });
        return;
    }

    ASSERT_WITH_MESSAGE(!m_isProcessingNetworkResponse, "Load should not be able to finish before we've validated the response");

    if (m_coreLoader->documentLoader()->applicationCacheHost().maybeLoadFallbackForError(m_coreLoader.get(), error))
        return;
    m_coreLoader->didFail(error);
}

void WebResourceLoader::didBlockAuthenticationChallenge()
{
    LOG(Network, "(WebProcess) WebResourceLoader::didBlockAuthenticationChallenge for '%s'", m_coreLoader->url().string().latin1().data());
    WEBRESOURCELOADER_RELEASE_LOG("didBlockAuthenticationChallenge:");

    m_coreLoader->didBlockAuthenticationChallenge();
}

void WebResourceLoader::stopLoadingAfterXFrameOptionsOrContentSecurityPolicyDenied(const ResourceResponse& response)
{
    LOG(Network, "(WebProcess) WebResourceLoader::stopLoadingAfterXFrameOptionsOrContentSecurityPolicyDenied for '%s'", m_coreLoader->url().string().latin1().data());
    WEBRESOURCELOADER_RELEASE_LOG("stopLoadingAfterXFrameOptionsOrContentSecurityPolicyDenied:");

    m_coreLoader->documentLoader()->stopLoadingAfterXFrameOptionsOrContentSecurityPolicyDenied(m_coreLoader->identifier(), response);
}

#if ENABLE(SHAREABLE_RESOURCE)
void WebResourceLoader::didReceiveResource(ShareableResource::Handle&& handle)
{
    const char* resourceURL = "<null>";
    CString resourceURLString;
    if (m_coreLoader) {
        resourceURLString = m_coreLoader->url().string().utf8();
        if (const char* data = resourceURLString.data())
            resourceURL = data;
    }
    fprintf(stderr, "[WRL] didReceiveResource: url=%s handleNull=%d\n", resourceURL, handle.isNull());
    LOG(Network, "(WebProcess) WebResourceLoader::didReceiveResource for '%s'", m_coreLoader->url().string().latin1().data());
    WEBRESOURCELOADER_RELEASE_LOG("didReceiveResource:");

    RefPtr<SharedBuffer> buffer = WTFMove(handle).tryWrapInSharedBuffer();
    fprintf(stderr, "[WRL] didReceiveResource: buffer=%p size=%zu\n", buffer.get(), buffer ? buffer->size() : 0);

    if (!buffer) {
        LOG_ERROR("Unable to create buffer from ShareableResource sent from the network process.");
        WEBRESOURCELOADER_RELEASE_LOG("didReceiveResource: Unable to create FragmentedSharedBuffer");
        if (auto* frame = m_coreLoader->frame()) {
            if (auto* page = frame->page())
                page->diagnosticLoggingClient().logDiagnosticMessage(WebCore::DiagnosticLoggingKeys::internalErrorKey(), WebCore::DiagnosticLoggingKeys::createSharedBufferFailedKey(), WebCore::ShouldSample::No);
        }
        m_coreLoader->didFail(internalError(m_coreLoader->request().url()));
        return;
    }

    Ref<WebResourceLoader> protect(*this);

    // Only send data to the didReceiveData callback if it exists.
    if (unsigned bufferSize = buffer->size())
        m_coreLoader->didReceiveData(buffer.releaseNonNull(), bufferSize, DataPayloadWholeResource);

    if (!m_coreLoader)
        return;

    NetworkLoadMetrics emptyMetrics;
    fprintf(stderr, "[WRL] didReceiveResource: calling didFinishLoading url=%s\n", resourceURL);
    m_coreLoader->didFinishLoading(emptyMetrics);
}
#endif

#if ENABLE(CONTENT_FILTERING)
void WebResourceLoader::contentFilterDidBlockLoad(const WebCore::ContentFilterUnblockHandler& unblockHandler, String&& unblockRequestDeniedScript, const ResourceError& error, const URL& blockedPageURL,  WebCore::SubstituteData&& substituteData)
{
    if (!m_coreLoader || !m_coreLoader->documentLoader())
        return;
    auto documentLoader = m_coreLoader->documentLoader();
    documentLoader->setBlockedPageURL(blockedPageURL);
    documentLoader->setSubstituteDataFromContentFilter(WTFMove(substituteData));
    documentLoader->handleContentFilterDidBlock(unblockHandler, WTFMove(unblockRequestDeniedScript));
    documentLoader->cancelMainResourceLoad(error);
}
#endif // ENABLE(CONTENT_FILTERING)

} // namespace WebKit

#undef WEBRESOURCELOADER_RELEASE_LOG
