/*
 * Copyright (C) 2016 Igalia S.L.
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
#include "NetworkSessionSoup.h"

#include "NetworkProcess.h"
#include "NetworkSessionCreationParameters.h"
#include "WebCookieManager.h"
#include "WebSocketTaskSoup.h"
#include <WebCore/DeprecatedGlobalSettings.h>
#include <WebCore/NetworkStorageSession.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/SoupNetworkSession.h>
#include <libsoup/soup.h>
#include <stdio.h>

namespace WebKit {
using namespace WebCore;

NetworkSessionSoup::NetworkSessionSoup(NetworkProcess& networkProcess, const NetworkSessionCreationParameters& parameters)
    : NetworkSession(networkProcess, parameters)
    , m_networkSession(makeUnique<SoupNetworkSession>(m_sessionID))
    , m_persistentCredentialStorageEnabled(parameters.persistentCredentialStorageEnabled)
{
    fprintf(stderr, "[NSSoup] ctor enter session=%" PRIu64 "\n", m_sessionID.toUInt64());
    auto* storageSession = networkStorageSession();
    fprintf(stderr, "[NSSoup] storageSession=%p\n", storageSession);
    ASSERT(storageSession);

    fprintf(stderr, "[NSSoup] setCookieAcceptPolicy begin\n");
    storageSession->setCookieAcceptPolicy(parameters.cookieAcceptPolicy);
    fprintf(stderr, "[NSSoup] setCookieAcceptPolicy done\n");

    fprintf(stderr, "[NSSoup] setIgnoreTLSErrors begin\n");
    setIgnoreTLSErrors(parameters.ignoreTLSErrors);
    fprintf(stderr, "[NSSoup] setIgnoreTLSErrors done\n");

    if (parameters.proxySettings.mode != SoupNetworkProxySettings::Mode::Default)
        setProxySettings(parameters.proxySettings);

    if (!parameters.cookiePersistentStoragePath.isEmpty()) {
        fprintf(stderr, "[NSSoup] setCookiePersistentStorage begin\n");
        setCookiePersistentStorage(parameters.cookiePersistentStoragePath, parameters.cookiePersistentStorageType);
        fprintf(stderr, "[NSSoup] setCookiePersistentStorage done\n");
    } else {
        fprintf(stderr, "[NSSoup] setCookieJar begin\n");
        m_networkSession->setCookieJar(storageSession->cookieStorage());
        fprintf(stderr, "[NSSoup] setCookieJar done\n");
    }

    if (!parameters.hstsStorageDirectory.isEmpty()) {
        fprintf(stderr, "[NSSoup] setHSTS begin\n");
        m_networkSession->setHSTSPersistentStorage(parameters.hstsStorageDirectory);
        fprintf(stderr, "[NSSoup] setHSTS done\n");
    }
    fprintf(stderr, "[NSSoup] ctor done\n");
}

NetworkSessionSoup::~NetworkSessionSoup()
{
}

SoupSession* NetworkSessionSoup::soupSession() const
{
    return m_networkSession->soupSession();
}

void NetworkSessionSoup::setCookiePersistentStorage(const String& storagePath, SoupCookiePersistentStorageType storageType)
{
    auto* storageSession = networkStorageSession();
    if (!storageSession)
        return;

    GRefPtr<SoupCookieJar> jar;
    switch (storageType) {
    case SoupCookiePersistentStorageType::Text:
        jar = adoptGRef(soup_cookie_jar_text_new(storagePath.utf8().data(), FALSE));
        break;
    case SoupCookiePersistentStorageType::SQLite:
        jar = adoptGRef(soup_cookie_jar_db_new(storagePath.utf8().data(), FALSE));
        break;
    }
    storageSession->setCookieStorage(WTFMove(jar));

    m_networkSession->setCookieJar(storageSession->cookieStorage());
}

void NetworkSessionSoup::clearCredentials(WallTime)
{
#if SOUP_CHECK_VERSION(2, 57, 1)
    soup_auth_manager_clear_cached_credentials(SOUP_AUTH_MANAGER(soup_session_get_feature(soupSession(), SOUP_TYPE_AUTH_MANAGER)));
#endif
}

#if USE(SOUP2)
static gboolean webSocketAcceptCertificateCallback(GTlsConnection* connection, GTlsCertificate* certificate, GTlsCertificateFlags errors, NetworkSessionSoup* session)
{
    if (DeprecatedGlobalSettings::allowsAnySSLCertificate())
        return TRUE;

    auto* soupMessage = static_cast<SoupMessage*>(g_object_get_data(G_OBJECT(connection), "wk-soup-message"));
    return !session->soupNetworkSession().checkTLSErrors(soupURIToURL(soup_message_get_uri(soupMessage)), certificate, errors);
}

static void webSocketMessageNetworkEventCallback(SoupMessage* soupMessage, GSocketClientEvent event, GIOStream* connection, NetworkSessionSoup* session)
{
    if (event != G_SOCKET_CLIENT_TLS_HANDSHAKING)
        return;

    g_object_set_data(G_OBJECT(connection), "wk-soup-message", soupMessage);
    g_signal_connect(connection, "accept-certificate", G_CALLBACK(webSocketAcceptCertificateCallback), session);
}
#endif

std::unique_ptr<WebSocketTask> NetworkSessionSoup::createWebSocketTask(WebPageProxyIdentifier, std::optional<FrameIdentifier> frameID, std::optional<PageIdentifier> pageID, NetworkSocketChannel& channel, const ResourceRequest& request, const String& protocol, const ClientOrigin&, bool, bool, OptionSet<WebCore::AdvancedPrivacyProtections>, ShouldRelaxThirdPartyCookieBlocking shouldRelaxThirdPartyCookieBlocking, StoredCredentialsPolicy)
{
    GRefPtr<SoupMessage> soupMessage = request.createSoupMessage(blobRegistry());
    if (!soupMessage)
        return nullptr;

    if (request.url().protocolIs("wss"_s)) {
#if USE(SOUP2)
        g_signal_connect(soupMessage.get(), "network-event", G_CALLBACK(webSocketMessageNetworkEventCallback), this);
#else
        g_signal_connect(soupMessage.get(), "accept-certificate", G_CALLBACK(+[](SoupMessage* message, GTlsCertificate* certificate, GTlsCertificateFlags errors,  NetworkSessionSoup* session) -> gboolean {
            if (DeprecatedGlobalSettings::allowsAnySSLCertificate())
                return TRUE;

            return !session->soupNetworkSession().checkTLSErrors(soup_message_get_uri(message), certificate, errors);
        }), this);
#endif
    }

#if ENABLE(TRACKING_PREVENTION)
    bool shouldBlockCookies = networkStorageSession()->shouldBlockCookies(request, frameID, pageID, shouldRelaxThirdPartyCookieBlocking);
    if (shouldBlockCookies)
        soup_message_disable_feature(soupMessage.get(), SOUP_TYPE_COOKIE_JAR);
#endif

    return makeUnique<WebSocketTask>(channel, request, soupSession(), soupMessage.get(), protocol);
}

void NetworkSessionSoup::setIgnoreTLSErrors(bool ignoreTLSErrors)
{
    m_networkSession->setIgnoreTLSErrors(ignoreTLSErrors);
}

void NetworkSessionSoup::allowSpecificHTTPSCertificateForHost(const CertificateInfo& certificateInfo, const String& host)
{
    m_networkSession->allowSpecificHTTPSCertificateForHost(certificateInfo, host);
}

void NetworkSessionSoup::setProxySettings(const SoupNetworkProxySettings& settings)
{
    m_networkSession->setProxySettings(settings);
}

} // namespace WebKit
