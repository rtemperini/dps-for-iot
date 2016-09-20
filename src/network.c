#include <assert.h>
#include <string.h>
#include <malloc.h>
#include <uv.h>
#include <dps/dps_dbg.h>
#include <dps/dps.h>
#include <dps/network.h>
#include "dps_node.h"

/*
 * Debug control for this module
 */
DPS_DEBUG_CONTROL(DPS_DEBUG_ON);

const char* DPS_NetAddrText(const struct sockaddr* addr)
{
    if (addr) {
        static char txt[INET6_ADDRSTRLEN + 8];
        uint16_t port;
        int ret;
        if (addr->sa_family == AF_INET6) {
            ret = uv_ip6_name((const struct sockaddr_in6*)addr, txt, sizeof(txt));
            port = ((const struct sockaddr_in6*)addr)->sin6_port;
        } else {
            ret = uv_ip4_name((const struct sockaddr_in*)addr, txt, sizeof(txt));
            port = ((const struct sockaddr_in*)addr)->sin_port;
        }
        if (ret) {
            return "Invalid address";
        }
        sprintf(txt + strlen(txt), "/%d", ntohs(port));
        return txt;
    } else {
        return "NULL";
    }
}

#define MAX_HOST_LEN    256  /* Per RFC 1034/1035 */
#define MAX_SERVICE_LEN  16  /* Per RFC 6335 section 5.1 */

typedef struct {
    DPS_Node* node;
    DPS_OnResolveAddressComplete cb;
    void* data;
    uv_getaddrinfo_t info;
    char host[MAX_HOST_LEN];
    char service[MAX_SERVICE_LEN];
} ResolverInfo;

static void GetAddrInfoCB(uv_getaddrinfo_t* req, int status, struct addrinfo* res)
{
    ResolverInfo* resolver = (ResolverInfo*)req->data;
    if (status == 0) {
        DPS_NodeAddress addr;
        if (res->ai_family == AF_INET6) {
            memcpy(&addr.inaddr, res->ai_addr, sizeof(struct sockaddr_in6));
        } else {
            memcpy(&addr.inaddr, res->ai_addr, sizeof(struct sockaddr_in));
        }
        resolver->cb(resolver->node, &addr, resolver->data);
        uv_freeaddrinfo(res);
    } else {
        DPS_ERRPRINT("uv_getaddrinfo failed %s\n", uv_err_name(status));
        resolver->cb(resolver->node, NULL, resolver->data);
    }
    free(resolver);
}

static void FreeHandle(uv_handle_t* handle)
{
    free(handle);
}

static void AsyncResolveAddress(uv_async_t* async)
{
    ResolverInfo* resolver = (ResolverInfo*)async->data;
    int r;
    struct addrinfo hints;

    DPS_DBGTRACE();

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    resolver->info.data = resolver;

    r = uv_getaddrinfo(async->loop, &resolver->info, GetAddrInfoCB, resolver->host, resolver->service, &hints);
    if (r) {
        DPS_ERRPRINT("uv_getaddrinfo call error %s\n", uv_err_name(r));
        resolver->cb(resolver->node, NULL, resolver->data);
        free(resolver);
    }
    uv_close((uv_handle_t*)async, FreeHandle);
}

DPS_Status DPS_ResolveAddress(DPS_Node* node, const char* host, const char* service, DPS_OnResolveAddressComplete cb, void* data)
{
    uv_async_t* async;
    ResolverInfo* resolver;

    DPS_DBGTRACE();

    if (!node->loop) {
        DPS_ERRPRINT("Cannot resolve address - node has not been started\n");
        return DPS_ERR_INVALID;
    }
    if (!service || !cb) {
        return DPS_ERR_NULL;
    }
    if (!host) {
        host = "localhost";
    }
    async = malloc(sizeof(uv_async_t));
    if (!async) {
        return DPS_ERR_RESOURCES;
    }
    resolver = calloc(1, sizeof(ResolverInfo));
    if (!resolver) {
        free(async);
        return DPS_ERR_RESOURCES;
    }
    strncpy(resolver->host, host, sizeof(resolver->host));
    strncpy(resolver->service, service, sizeof(resolver->service));
    resolver->node = node;
    resolver->cb = cb;
    resolver->data = data;
    /*
     * Async callback
     */
    if (uv_async_init(node->loop, async, AsyncResolveAddress)) {
        free(async);
        free(resolver);
        return DPS_ERR_RESOURCES;
    }
    async->data = resolver;
    if (uv_async_send(async)) {
        free(async);
        free(resolver);
        return DPS_ERR_FAILURE;
    } else {
        return DPS_OK;
    }
}
