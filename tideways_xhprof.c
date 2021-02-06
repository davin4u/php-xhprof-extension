#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "SAPI.h"
#include "ext/standard/info.h"
#include "php_tideways_xhprof.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
/*
#include "php_string.h"
#include "php_var.h"
#include "zend_smart_str.h"
#include "basic_functions.h"*/

#include "zend_smart_str.h"
#include "ext/json/php_json.h"
#include "ext/json/php_json_encoder.h"
#include "ext/json/php_json_parser.h"
#include "ext/json/json_arginfo.h"

ZEND_DECLARE_MODULE_GLOBALS(tideways_xhprof)

#include "tracing.h"
#if PHP_VERSION_ID >= 80000
#include "tideways_xhprof_arginfo.h"
#else
#include "tideways_xhprof_legacy_arginfo.h"
#endif

PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("tideways_xhprof.clock_use_rdtsc", "0", PHP_INI_SYSTEM, OnUpdateBool, clock_use_rdtsc, zend_tideways_xhprof_globals, tideways_xhprof_globals)
PHP_INI_END()

static void (*_zend_execute_internal) (zend_execute_data *execute_data, zval *return_value);
ZEND_DLEXPORT void tideways_xhprof_execute_internal(zend_execute_data *execute_data, zval *return_value);

#if PHP_VERSION_ID >= 80000
#include "zend_observer.h"

static void tracer_observer_begin(zend_execute_data *ex) {
    if (!TXRG(enabled)) {
        return;
    }

    tracing_enter_frame_callgraph(NULL, ex);
}

static void tracer_observer_end(zend_execute_data *ex, zval *return_value) {
    if (!TXRG(enabled)) {
        return;
    }

    if (TXRG(callgraph_frames)) {
        tracing_exit_frame_callgraph(TSRMLS_C);
    }
}


static zend_observer_fcall_handlers tracer_observer(zend_execute_data *execute_data) {
    zend_function *func = execute_data->func;

    if (!func->common.function_name) {
        return (zend_observer_fcall_handlers){NULL, NULL};
    }

    return (zend_observer_fcall_handlers){tracer_observer_begin, tracer_observer_end};
}
#else
static void (*_zend_execute_ex) (zend_execute_data *execute_data);

void tideways_xhprof_execute_ex (zend_execute_data *execute_data) {
    zend_execute_data *real_execute_data = execute_data;
    int is_profiling = 0;

    if (!TXRG(enabled)) {
        _zend_execute_ex(execute_data TSRMLS_CC);
        return;
    }

    is_profiling = tracing_enter_frame_callgraph(NULL, real_execute_data TSRMLS_CC);

    _zend_execute_ex(execute_data TSRMLS_CC);

    if (is_profiling == 1 && TXRG(callgraph_frames)) {
        tracing_exit_frame_callgraph(TSRMLS_C);
    }
}
#endif

void savelog (char data[25000]) {
    FILE * fPtr;

    fPtr = fopen("/tmp/log.txt", "a");

    if(fPtr == NULL)
    {
        /* File not created hence exit */
        printf("Unable to create file.\n");
        exit(EXIT_FAILURE);
    }

    fputs(data, fPtr);
    fputs("\n", fPtr);

    fclose(fPtr);
}

PHP_FUNCTION(tideways_xhprof_enable)
{
    zend_long flags = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &flags) == FAILURE) {
        return;
    }

    tracing_begin(flags TSRMLS_CC);
    tracing_enter_root_frame(TSRMLS_C);
}

PHP_FUNCTION(tideways_xhprof_disable)
{
    tracing_end(TSRMLS_C);

    array_init(return_value);

    tracing_callgraph_append_to_array(return_value TSRMLS_CC);

    send_agent_msg(return_value);
}

/*
void format_tracing_data(zval *struc, char[] *formated, int *index)
{
    HashTable *myht;
    zend_ulong num;
	zend_string *key;
	zval *val;

    switch (Z_TYPE_P(struc)) {
        case IS_LONG:
            append_format_long(formated, index, Z_LVAL_P(struc));

            break;

        case IS_ARRAY:
			myht = Z_ARRVAL_P(struc);

			ZEND_HASH_FOREACH_KEY_VAL(myht, num, key, val) {
				format_array_element(formated, index, val, num, key);
			} ZEND_HASH_FOREACH_END();

			break;
    }
}

void format_array_element(char[] *formated, int *index, zval *zv, zend_ulong index, zend_string *key)
{
	if (key == NULL) {

	} else {
        append_format_string(formated, index, ZSTR_VAL(key));
	}

	format_tracing_data(zv, formated);
}

void append_format_long(char[] *formated, int *index, long v)
{
    union {
        char myByte[512];
        long mylong;
    } vu;

    vu.mylong = v;

    if (index + strlen(vu.myByte) < 8192) {
        int i;
        for (i = 0; i < strlen(vu.myByte); i++) {
            formated[index] = vu.myByte[i];
            index++;
        }
    }
}

void append_format_string(char[] *formated, int *index, char[] str)
{
    if (index + strlen(str) < 8192) {
        int i;
        for (i = 0; i < strlen(str); i++) {
            formated[index] = str[i];
            index++;
        }
    }
}
*/

void send_agent_msg(zval *struc)
{
    int fd;
	struct sockaddr_un addr;
	int ret;
	char buff[8192];
	struct sockaddr_un from;
	int ok = 1;
	int len;
    char err[10];
    char errtext[100];

    php_json_encoder encoder;
	smart_str buf = {0};

    php_json_encode_init(&encoder);
	encoder.max_depth = 10;

    php_json_encode_zval(&buf, struc, 0, &encoder);

    smart_str_0(&buf);

    if (buf.s) {
        savelog(buf.s);
    }

    savelog("s1");

    if ((fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
		ok = 0;
	}

    savelog("s2");

    if (ok) {
        savelog("s2ok");

		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strcpy(addr.sun_path, CLIENT_SOCK_FILE);
		unlink(CLIENT_SOCK_FILE);
		if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			ok = 0;
		}
	}

    savelog("s3");

    if (ok) {
        savelog("s3ok");
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strcpy(addr.sun_path, SERVER_SOCK_FILE);
		if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            sprintf(err,"%ld", errno);
            savelog(err);
            savelog(strerror(errno));
            ok = 0;
		}
	}

    savelog("s4");

    if (ok) {
        savelog("s4ok");
		strcpy(buff, formated);
		if (send(fd, buff, strlen(buff)+1, 0) == -1) {
			ok = 0;
		}
	}

    savelog("s5");

    if (ok) {
        savelog("s5ok");
		if ((len = recv(fd, buff, 8192, 0)) < 0) {
			ok = 0;
		}
	}

	if (fd >= 0) {
		close(fd);
	}

	unlink (CLIENT_SOCK_FILE);
}

PHP_GINIT_FUNCTION(tideways_xhprof)
{
#if defined(COMPILE_DL_TIDEWAYS_XHPROF) && defined(ZTS)
     ZEND_TSRMLS_CACHE_UPDATE();
#endif
     tideways_xhprof_globals->root = NULL;
     tideways_xhprof_globals->callgraph_frames = NULL;
     tideways_xhprof_globals->frame_free_list = NULL;
}

PHP_MINIT_FUNCTION(tideways_xhprof)
{
    REGISTER_INI_ENTRIES();

    REGISTER_LONG_CONSTANT("TIDEWAYS_XHPROF_FLAGS_MEMORY", TIDEWAYS_XHPROF_FLAGS_MEMORY, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("TIDEWAYS_XHPROF_FLAGS_MEMORY_MU", TIDEWAYS_XHPROF_FLAGS_MEMORY_MU, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("TIDEWAYS_XHPROF_FLAGS_MEMORY_PMU", TIDEWAYS_XHPROF_FLAGS_MEMORY_PMU, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("TIDEWAYS_XHPROF_FLAGS_CPU", TIDEWAYS_XHPROF_FLAGS_CPU, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("TIDEWAYS_XHPROF_FLAGS_NO_BUILTINS", TIDEWAYS_XHPROF_FLAGS_NO_BUILTINS, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("TIDEWAYS_XHPROF_FLAGS_MEMORY_ALLOC", TIDEWAYS_XHPROF_FLAGS_MEMORY_ALLOC, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("TIDEWAYS_XHPROF_FLAGS_MEMORY_ALLOC_AS_MU", TIDEWAYS_XHPROF_FLAGS_MEMORY_ALLOC_AS_MU, CONST_CS | CONST_PERSISTENT);

    _zend_execute_internal = zend_execute_internal;
    zend_execute_internal = tideways_xhprof_execute_internal;

#if PHP_VERSION_ID >= 80000
    zend_observer_fcall_register(tracer_observer);
#else
    _zend_execute_ex = zend_execute_ex;
    zend_execute_ex = tideways_xhprof_execute_ex;
#endif

    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(tideways_xhprof)
{
    return SUCCESS;
}

PHP_RINIT_FUNCTION(tideways_xhprof)
{
    tracing_request_init(TSRMLS_C);
    TXRG(clock_source) = determine_clock_source(TXRG(clock_use_rdtsc));

    CG(compiler_options) = CG(compiler_options) | ZEND_COMPILE_NO_BUILTINS;

    return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(tideways_xhprof)
{
    int i = 0;
    xhprof_callgraph_bucket *bucket;

    tracing_end(TSRMLS_C);

    for (i = 0; i < TIDEWAYS_XHPROF_CALLGRAPH_SLOTS; i++) {
        bucket = TXRG(callgraph_buckets)[i];

        while (bucket) {
            TXRG(callgraph_buckets)[i] = bucket->next;
            tracing_callgraph_bucket_free(bucket);
            bucket = TXRG(callgraph_buckets)[i];
        }
    }

    tracing_request_shutdown();

    return SUCCESS;
}

static int tideways_xhprof_info_print(const char *str) /* {{{ */
{
    return php_output_write(str, strlen(str));
}

PHP_MINFO_FUNCTION(tideways_xhprof)
{
    php_info_print_table_start();
    php_info_print_table_row(2, "Version", PHP_TIDEWAYS_XHPROF_VERSION);

    switch (TXRG(clock_source)) {
        case TIDEWAYS_XHPROF_CLOCK_TSC:
            php_info_print_table_row(2, "Clock Source", "tsc");
            break;
        case TIDEWAYS_XHPROF_CLOCK_CGT:
            php_info_print_table_row(2, "Clock Source", "clock_gettime");
            break;
        case TIDEWAYS_XHPROF_CLOCK_GTOD:
            php_info_print_table_row(2, "Clock Source", "gettimeofday");
            break;
        case TIDEWAYS_XHPROF_CLOCK_MACH:
            php_info_print_table_row(2, "Clock Source", "mach");
            break;
        case TIDEWAYS_XHPROF_CLOCK_QPC:
            php_info_print_table_row(2, "Clock Source", "Query Performance Counter");
            break;
        case TIDEWAYS_XHPROF_CLOCK_NONE:
            php_info_print_table_row(2, "Clock Source", "none");
            break;
    }
    php_info_print_table_end();

    php_info_print_box_start(0);

    if (!sapi_module.phpinfo_as_text) {
        tideways_xhprof_info_print("<a href=\"https://tideways.io\"><img border=0 src=\"");
        tideways_xhprof_info_print(TIDEWAYS_LOGO_DATA_URI "\" alt=\"Tideways logo\" /></a>\n");
    }

    tideways_xhprof_info_print("Tideways is a PHP Profiler, Monitoring and Exception Tracking Software.");
    tideways_xhprof_info_print(!sapi_module.phpinfo_as_text?"<br /><br />":"\n\n");
    tideways_xhprof_info_print("The 'tideways_xhprof' extension provides a subset of the functionality of our commercial Tideways offering in a modern, optimized fork of the XHProf extension from Facebook as open-source. (c) Tideways GmbH 2014-2017, (c) Facebook 2009");

    if (!sapi_module.phpinfo_as_text) {
        tideways_xhprof_info_print("<br /><br /><strong>Register for a free trial on <a style=\"background-color: inherit\" href=\"https://tideways.io\">https://tideways.io</a></strong>");
    } else {
        tideways_xhprof_info_print("\n\nRegister for a free trial on https://tideways.io\n\n");
    }

    php_info_print_box_end();

}

ZEND_DLEXPORT void tideways_xhprof_execute_internal(zend_execute_data *execute_data, zval *return_value) {
    int is_profiling = 1;

    if (!TXRG(enabled) || (TXRG(flags) & TIDEWAYS_XHPROF_FLAGS_NO_BUILTINS) > 0) {
        execute_internal(execute_data, return_value TSRMLS_CC);
        return;
    }

    is_profiling = tracing_enter_frame_callgraph(NULL, execute_data TSRMLS_CC);

    if (!_zend_execute_internal) {
        execute_internal(execute_data, return_value TSRMLS_CC);
    } else {
        _zend_execute_internal(execute_data, return_value TSRMLS_CC);
    }

    if (is_profiling == 1 && TXRG(callgraph_frames)) {
        tracing_exit_frame_callgraph(TSRMLS_C);
    }
}

const zend_function_entry tideways_xhprof_functions[] = {
    PHP_FE(tideways_xhprof_enable,	arginfo_tideways_xhprof_enable)
    PHP_FE(tideways_xhprof_disable,	arginfo_tideways_xhprof_disable)
    PHP_FE_END
};

zend_module_entry tideways_xhprof_module_entry = {
    STANDARD_MODULE_HEADER,
    "tideways_xhprof",
    tideways_xhprof_functions,
    PHP_MINIT(tideways_xhprof),
    PHP_MSHUTDOWN(tideways_xhprof),
    PHP_RINIT(tideways_xhprof),
    PHP_RSHUTDOWN(tideways_xhprof),
    PHP_MINFO(tideways_xhprof),
    PHP_TIDEWAYS_XHPROF_VERSION,
    PHP_MODULE_GLOBALS(tideways_xhprof),
    PHP_GINIT(tideways_xhprof),
    NULL,
    NULL,
    STANDARD_MODULE_PROPERTIES_EX
};

#ifdef COMPILE_DL_TIDEWAYS_XHPROF
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(tideways_xhprof)
#endif
