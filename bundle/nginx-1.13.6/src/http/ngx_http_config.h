
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HTTP_CONFIG_H_INCLUDED_
#define _NGX_HTTP_CONFIG_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
一个配置需要指定所属
#define NGX_HTTP_MAIN_CONF_OFFSET  offsetof(ngx_http_conf_ctx_t, main_conf)
#define NGX_HTTP_SRV_CONF_OFFSET   offsetof(ngx_http_conf_ctx_t, srv_conf)
#define NGX_HTTP_LOC_CONF_OFFSET   offsetof(ngx_http_conf_ctx_t, loc_conf)
下边结构中的双重指针有没有发现？首先各个模块的配置肯定是void *因为强转才能用
但为什么不是
typedef struct {
    void        *main_conf;
    void        *srv_conf;
    void        *loc_conf;
} ngx_http_conf_ctx_t;
因为：ngx_http_conf_ctx_t不是一个http模块独占的，而是所有的http模块公用
每个成员都是一个数组，每个http 模块都有自己的下标，然后通过下标得到自己的void *

下标是这个： module.ctx_index ，模块在自己类别中的索引，所有http 模块中所得到的序号
*/
typedef struct {
    void        **main_conf;
    void        **srv_conf;
    void        **loc_conf;
} ngx_http_conf_ctx_t;

//http模块都在这个结构中定义,可以看出都是在折腾配置
//http配置包括几个位置，包括：main，srv，loc 。。。。
//有的命令会 同时支持在 上层进行配置，这样一般为了下层如果不进行配置就从上层配置进行复制，
//如果底层进行过配置，就会覆盖上层的配置
//当然，也可以根据上层的配置  适当的调整下层的配置
//所以：下边的src 和 loc 使用用来根据上层配置调节本层配置的
typedef struct {
    ngx_int_t   (*preconfiguration)(ngx_conf_t *cf);
	//postconfiguration是个关键的时机，因为此时已经配置完成了，可以根据配置做事了
	//比如把自己注册 进http流程 什么什么的
    ngx_int_t   (*postconfiguration)(ngx_conf_t *cf);

    //main配置，因为http没有更上层了，所以没有merge，只有init
    void       *(*create_main_conf)(ngx_conf_t *cf);
    char       *(*init_main_conf)(ngx_conf_t *cf, void *conf);

    //srv配置和merge
    void       *(*create_srv_conf)(ngx_conf_t *cf);
    char       *(*merge_srv_conf)(ngx_conf_t *cf, void *prev, void *conf);

    //loc配置和merge
    void       *(*create_loc_conf)(ngx_conf_t *cf);
    char       *(*merge_loc_conf)(ngx_conf_t *cf, void *prev, void *conf);
} ngx_http_module_t;

//打在ngx_module_s结构的type域上，打上以后ctx域必须是ngx_http_module_t，往上看两行
#define NGX_HTTP_MODULE           0x50545448   /* "HTTP" */

//命令所支持的配置区域
#define NGX_HTTP_MAIN_CONF        0x02000000
#define NGX_HTTP_SRV_CONF         0x04000000
#define NGX_HTTP_LOC_CONF         0x08000000
#define NGX_HTTP_UPS_CONF         0x10000000
#define NGX_HTTP_SIF_CONF         0x20000000
#define NGX_HTTP_LIF_CONF         0x40000000
#define NGX_HTTP_LMT_CONF         0x80000000

/*
typedef struct {
    void        **main_conf;
    void        **srv_conf;
    void        **loc_conf;
} ngx_http_conf_ctx_t;
*/
#define NGX_HTTP_MAIN_CONF_OFFSET  offsetof(ngx_http_conf_ctx_t, main_conf)
#define NGX_HTTP_SRV_CONF_OFFSET   offsetof(ngx_http_conf_ctx_t, srv_conf)
#define NGX_HTTP_LOC_CONF_OFFSET   offsetof(ngx_http_conf_ctx_t, loc_conf)

/*如果参数是ngx_conf_s的ctx，那么使用下列宏取配置*/
#define ngx_http_get_module_main_conf(r, module)                             \
    (r)->main_conf[module.ctx_index]
#define ngx_http_get_module_srv_conf(r, module)  (r)->srv_conf[module.ctx_index]
#define ngx_http_get_module_loc_conf(r, module)  (r)->loc_conf[module.ctx_index]

/*
cf 是这个类型：ngx_conf_s, 所有模块的配置都使用
取得ngx_http_conf_ctx_t后再使用
*/
#define ngx_http_conf_get_module_main_conf(cf, module)                        \
    ((ngx_http_conf_ctx_t *) cf->ctx)->main_conf[module.ctx_index]
#define ngx_http_conf_get_module_srv_conf(cf, module)                         \
    ((ngx_http_conf_ctx_t *) cf->ctx)->srv_conf[module.ctx_index]
#define ngx_http_conf_get_module_loc_conf(cf, module)                         \
    ((ngx_http_conf_ctx_t *) cf->ctx)->loc_conf[module.ctx_index]

//
#define ngx_http_cycle_get_module_main_conf(cycle, module)                    \
    (cycle->conf_ctx[ngx_http_module.index] ?                                 \
        ((ngx_http_conf_ctx_t *) cycle->conf_ctx[ngx_http_module.index])      \
            ->main_conf[module.ctx_index]:                                    \
        NULL)


#endif /* _NGX_HTTP_CONFIG_H_INCLUDED_ */
