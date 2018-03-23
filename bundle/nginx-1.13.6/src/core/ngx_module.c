
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Maxim Dounin
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


#define NGX_MAX_DYNAMIC_MODULES  128


static ngx_uint_t ngx_module_index(ngx_cycle_t *cycle);
static ngx_uint_t ngx_module_ctx_index(ngx_cycle_t *cycle, ngx_uint_t type,
    ngx_uint_t index);


ngx_uint_t         ngx_max_module;
static ngx_uint_t  ngx_modules_n;

//main函数中调用，主要作用是给模块编号和命名，
//再设置全局变量:
//ngx_max_module -- 最大模块数
//ngx_modules_n -- 当前模块数
ngx_int_t
ngx_preinit_modules(void)
{
    ngx_uint_t  i;

	//给所有的静态模块设置编号，也设置模块的名字（模块的名字来自configure配置生成）
	//循环到空位置
    for (i = 0; ngx_modules[i]; i++) {
        ngx_modules[i]->index = i;
        ngx_modules[i]->name = ngx_module_names[i];
    }
	//设置现在的模块数
    ngx_modules_n = i;
	//算上可能的动态模块数，最多允许多少个模块
    ngx_max_module = ngx_modules_n + NGX_MAX_DYNAMIC_MODULES;

    return NGX_OK;
}

//往cycle里设置模块信息，至此模块就导入生命周期了
//平时用都是在cycle中使用
ngx_int_t
ngx_cycle_modules(ngx_cycle_t *cycle)
{
    /*
     * create a list of modules to be used for this cycle,
     * copy static modules to it
     */
	//创建模块数组（指针数组），末尾多了一个位置
    cycle->modules = ngx_pcalloc(cycle->pool, (ngx_max_module + 1)
                                              * sizeof(ngx_module_t *));
    if (cycle->modules == NULL) {
        return NGX_ERROR;
    }
	//拷贝已有的静态模块信息进来，动态模块的事没提
    ngx_memcpy(cycle->modules, ngx_modules,
               ngx_modules_n * sizeof(ngx_module_t *));
	//现有模块的数量
    cycle->modules_n = ngx_modules_n;

    return NGX_OK;
}

//调用各个模块的init_module
ngx_int_t
ngx_init_modules(ngx_cycle_t *cycle)
{
    ngx_uint_t  i;

    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->init_module) {
			//执行各个模块的初始化
            if (cycle->modules[i]->init_module(cycle) != NGX_OK) {
                return NGX_ERROR;
            }
        }
    }

    return NGX_OK;
}

//在cycle中数 某种 模块的数量
//也在给该种类下模块设置ctx_index
ngx_int_t
ngx_count_modules(ngx_cycle_t *cycle, ngx_uint_t type)
{
    ngx_uint_t     i, next, max;
    ngx_module_t  *module;

    next = 0;
    max = 0;

    /* count appropriate modules, set up their indices */
	//遍历所有的模块
    for (i = 0; cycle->modules[i]; i++) {
        module = cycle->modules[i];

        if (module->type != type) {	//必须是指定类型的
            continue;
        }

		//如果ctx_index 已经被设置，则收集最大ctx_index到max，next也能顺序的往前顶。
        if (module->ctx_index != NGX_MODULE_UNSET_INDEX) {

            /* if ctx_index was assigned, preserve it */
			//收集最大的ctx index值
            if (module->ctx_index > max) {
                max = module->ctx_index;
            }
			//next 是连续的ctx_index目前最大值
            if (module->ctx_index == next) {
                next++;
            }

            continue;
        }

        /* search for some free index */
		//否则就给模块分配一个ctx_index，从next 开始分配下一个ctx_index, 估计是为了省力
		//从next开始找下一个空闲的ctx_index
        module->ctx_index = ngx_module_ctx_index(cycle, type, next);

		//同样要收集ctx_index最大值
        if (module->ctx_index > max) {
            max = module->ctx_index;
        }

		//next 指向下一个待搜索ctx_index
        next = module->ctx_index + 1;
    }

    /*
     * make sure the number returned is big enough for previous
     * cycle as well, else there will be problems if the number
     * will be stored in a global variable (as it's used to be)
     * and we'll have to roll back to the previous cycle
     */
	//old cycle 检查max
    if (cycle->old_cycle && cycle->old_cycle->modules) {

        for (i = 0; cycle->old_cycle->modules[i]; i++) {
            module = cycle->old_cycle->modules[i];

            if (module->type != type) {
                continue;
            }

            if (module->ctx_index > max) {
                max = module->ctx_index;
            }
        }
    }

    /* prevent loading of additional modules */
	//模块被使用了，后边不再接受动态模块了
    cycle->modules_used = 1;

	//返回ctx 的量
    return max + 1;
}


//添加静态模块，模块已经被加载，order 是顺序么？
ngx_int_t
ngx_add_module(ngx_conf_t *cf, ngx_str_t *file, ngx_module_t *module,
    char **order)
{
    void               *rv;
    ngx_uint_t          i, m, before;
    ngx_core_module_t  *core_module;

	//模块数量不能太多
    if (cf->cycle->modules_n >= ngx_max_module) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "too many modules loaded");
        return NGX_ERROR;
    }
	//检查模块版本
    if (module->version != nginx_version) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "module \"%V\" version %ui instead of %ui",
                           file, module->version, (ngx_uint_t) nginx_version);
        return NGX_ERROR;
    }
	//对比signature
    if (ngx_strcmp(module->signature, NGX_MODULE_SIGNATURE) != 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "module \"%V\" is not binary compatible",
                           file);
        return NGX_ERROR;
    }
	//按照模块名字，模块不能重复加载
    for (m = 0; cf->cycle->modules[m]; m++) {
        if (ngx_strcmp(cf->cycle->modules[m]->name, module->name) == 0) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "module \"%s\" is already loaded",
                               module->name);
            return NGX_ERROR;
        }
    }

    /*
     * if the module wasn't previously loaded, assign an index
     */
	//如果模块没有编号，动态模块基本上都没有提前编号，需要分配模块编号
	//模块的索引，可能提前被设置好了
	//那么模块的安装位置，和模块的index 就不是一致对应了
	if (module->index == NGX_MODULE_UNSET_INDEX) {
        module->index = ngx_module_index(cf->cycle);

        if (module->index >= ngx_max_module) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "too many modules loaded");
            return NGX_ERROR;
        }
    }

    /*
     * put the module into the cycle->modules array
     */
	//当前的模块数，添加模块前的计数，如果没有其他原因，就在这个位置安装了
    before = cf->cycle->modules_n;

	//如果有指定order 数组，则
    if (order) {
        for (i = 0; order[i]; i++) {
            if (ngx_strcmp(order[i], module->name) == 0) {	//如果找到同名的，就进入到下一个索引，并结束
                i++;
                break;
            }
        }

        for ( /* void */ ; order[i]; i++) {	//然后继续遍历剩余的order
#if 0
            ngx_log_debug2(NGX_LOG_DEBUG_CORE, cf->log, 0,
                           "module: %s before %s",
                           module->name, order[i]);
#endif
			//后续的order都要，扫描之前已经注册的模块
            for (m = 0; m < before; m++) {
                if (ngx_strcmp(cf->cycle->modules[m]->name, order[i]) == 0) {
					//如果不幸找到了，那么当前模块需要排在它的前边，before 位置要提前
                    ngx_log_debug3(NGX_LOG_DEBUG_CORE, cf->log, 0,
                                   "module: %s before %s:%i",
                                   module->name, order[i], m);

                    before = m;		//这里修改了before
                    break;
                }
            }
        }
    }

    /* put the module before modules[before] */
	//原来这个位置是相等的，如果不等了，都往后移动一个位置，腾地方。
    if (before != cf->cycle->modules_n) {
        ngx_memmove(&cf->cycle->modules[before + 1],
                    &cf->cycle->modules[before],
                    (cf->cycle->modules_n - before) * sizeof(ngx_module_t *));
    }

	//将模块安装到cycle中
    cf->cycle->modules[before] = module;
    cf->cycle->modules_n++;

	//最后加载的模块如果是core模块
    if (module->type == NGX_CORE_MODULE) {

        /*
         * we are smart enough to initialize core modules;
         * other modules are expected to be loaded before
         * initialization - e.g., http modules must be loaded
         * before http{} block
         */
		//模块的ctx中保存着 core模块特有的ctx ngx_core_module_t *
        core_module = module->ctx;

        if (core_module->create_conf) {
			//core 模块常见配置块
            rv = core_module->create_conf(cf->cycle);
            if (rv == NULL) {
                return NGX_ERROR;
            }
			//给这个core模块设置 关联配置块
            cf->cycle->conf_ctx[module->index] = rv;
        }
    }

    return NGX_OK;
}

//分配模块index
static ngx_uint_t
ngx_module_index(ngx_cycle_t *cycle)
{
    ngx_uint_t     i, index;
    ngx_module_t  *module;

    index = 0;	//index 从0开始向后扫描

again:

    /* find an unused index */
	//循环现有的模块
    for (i = 0; cycle->modules[i]; i++) {
        module = cycle->modules[i];
		//如果发现了index，就从新 搜索下一个index
        if (module->index == index) {
            index++;
            goto again;
        }
    }

    /* check previous cycle */
	//如果有前一个cycle
    if (cycle->old_cycle && cycle->old_cycle->modules) {
		//搜索index，搜索到就重新搜索下一个
        for (i = 0; cycle->old_cycle->modules[i]; i++) {
            module = cycle->old_cycle->modules[i];

            if (module->index == index) {
                index++;
                goto again;
            }
        }
    }

	//最终没有找到的index，就是最小没在使用的
    return index;
}


//搜索可用的ctx index, 最小的空闲ctx_index
static ngx_uint_t
ngx_module_ctx_index(ngx_cycle_t *cycle, ngx_uint_t type, ngx_uint_t index)
{
    ngx_uint_t     i;
    ngx_module_t  *module;

	//区别是index是送进来的
again:

    /* find an unused ctx_index */
	//搜索已有的模块，找index
    for (i = 0; cycle->modules[i]; i++) {
        module = cycle->modules[i];

        if (module->type != type) {		//必须是指定类型的
            continue;
        }

        if (module->ctx_index == index) {
            index++;
            goto again;
        }
    }

    /* check previous cycle */
	//old cycle 也要查找
    if (cycle->old_cycle && cycle->old_cycle->modules) {

        for (i = 0; cycle->old_cycle->modules[i]; i++) {
            module = cycle->old_cycle->modules[i];

            if (module->type != type) {
                continue;
            }

            if (module->ctx_index == index) {
                index++;
                goto again;
            }
        }
    }

	//最小的未使用index被返回
    return index;
}

