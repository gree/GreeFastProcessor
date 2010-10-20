dnl $Id$
dnl config.m4 for extension gree_fastprocessor

PHP_ARG_ENABLE(gree_fastprocessor, whether to enable gree_fastprocessor support,
[  --enable-gree-fast-processor                 Enable fast processor (GREE proprietary)])

if test "$PHP_GREE_FASTPROCESSOR" != "no"; then
  GREE_FASTPROCESSOR_BUILDTIME=`LC_TIME=C date +"%c"`
  PHP_NEW_EXTENSION(gree_fastprocessor,
      php_gree_fastprocessor.c,$ext_shared,,[-DHAVE_CONFIG_H -DGREE_FASTPROCESSOR_BUILDTIME=\\"\\\\\\"$GREE_FASTPROCESSOR_BUILDTIME\\\\\\"\\"])

  AC_CHECK_HEADERS([assert.h memory.h string.h stddef.h stdio.h string.h sys/uio.h sys/epoll.h pthread.h])
  AC_CHECK_FUNCS([memchr])
fi
dnl vim: sts=2 sw=2 ts=2 et fdm=marker
