#!/usr/bin/env bash

################################################################################
#  THIS FILE IS 100% GENERATED BY ZPROJECT; DO NOT EDIT EXCEPT EXPERIMENTALLY  #
#  READ THE ZPROJECT/README.MD FOR INFORMATION ABOUT MAKING PERMANENT CHANGES. #
################################################################################

set -e

# Set this to enable verbose profiling
[ -n "${CI_TIME-}" ] || CI_TIME=""
case "$CI_TIME" in
    [Yy][Ee][Ss]|[Oo][Nn]|[Tt][Rr][Uu][Ee])
        CI_TIME="time -p " ;;
    [Nn][Oo]|[Oo][Ff][Ff]|[Ff][Aa][Ll][Ss][Ee])
        CI_TIME="" ;;
esac

# Set this to enable verbose tracing
[ -n "${CI_TRACE-}" ] || CI_TRACE="no"
case "$CI_TRACE" in
    [Nn][Oo]|[Oo][Ff][Ff]|[Ff][Aa][Ll][Ss][Ee])
        set +x ;;
    [Yy][Ee][Ss]|[Oo][Nn]|[Tt][Rr][Uu][Ee])
        set -x ;;
esac

case "$BUILD_TYPE" in
default|default-Werror|default-with-docs|valgrind|clang-format-check)
    LANG=C
    LC_ALL=C
    export LANG LC_ALL

    if [ -d "./tmp" ]; then
        rm -rf ./tmp
    fi
    mkdir -p tmp
    BUILD_PREFIX=$PWD/tmp

    PATH="`echo "$PATH" | sed -e 's,^/usr/lib/ccache/?:,,' -e 's,:/usr/lib/ccache/?:,,' -e 's,:/usr/lib/ccache/?$,,' -e 's,^/usr/lib/ccache/?$,,'`"
    CCACHE_PATH="$PATH"
    CCACHE_DIR="${HOME}/.ccache"
    # Use tools from prerequisites we might have built
    PATH="${BUILD_PREFIX}/sbin:${BUILD_PREFIX}/bin:${PATH}"
    export CCACHE_PATH CCACHE_DIR PATH
    HAVE_CCACHE=no
    if which ccache && ls -la /usr/lib/ccache ; then
        HAVE_CCACHE=yes
    fi
    mkdir -p "${CCACHE_DIR}" || HAVE_CCACHE=no

    if [ "$HAVE_CCACHE" = yes ] && [ -d "$CCACHE_DIR" ]; then
        echo "CCache stats before build:"
        ccache -s || true
    fi

    CONFIG_OPTS=()
    COMMON_CFLAGS=""
    EXTRA_CFLAGS=""
    EXTRA_CPPFLAGS=""
    EXTRA_CXXFLAGS=""

    is_gnucc() {
        if [ -n "$1" ] && "$1" --version 2>&1 | grep 'Free Software Foundation' > /dev/null ; then true ; else false ; fi
    }

    COMPILER_FAMILY=""
    if [ -n "$CC" -a -n "$CXX" ]; then
        if is_gnucc "$CC" && is_gnucc "$CXX" ; then
            COMPILER_FAMILY="GCC"
            export CC CXX
        fi
    else
        if is_gnucc "gcc" && is_gnucc "g++" ; then
            # Autoconf would pick this by default
            COMPILER_FAMILY="GCC"
            [ -n "$CC" ] || CC=gcc
            [ -n "$CXX" ] || CXX=g++
            export CC CXX
        elif is_gnucc "cc" && is_gnucc "c++" ; then
            COMPILER_FAMILY="GCC"
            [ -n "$CC" ] || CC=cc
            [ -n "$CXX" ] || CXX=c++
            export CC CXX
        fi
    fi

    if [ -n "$CPP" ] ; then
        [ -x "$CPP" ] && export CPP
    else
        if is_gnucc "cpp" ; then
            CPP=cpp && export CPP
        fi
    fi

    CONFIG_OPT_WERROR="--enable-Werror=no"
    if [ "$BUILD_TYPE" == "default-Werror" ] ; then
        case "${COMPILER_FAMILY}" in
            GCC)
                echo "NOTE: Enabling ${COMPILER_FAMILY} compiler pedantic error-checking flags for BUILD_TYPE='$BUILD_TYPE'" >&2
                CONFIG_OPT_WERROR="--enable-Werror=yes"
                CONFIG_OPTS+=("--enable-Werror=yes")
                ;;
            *)
                echo "WARNING: Current compiler is not GCC, might not enable pedantic error-checking flags for BUILD_TYPE='$BUILD_TYPE'" >&2
                CONFIG_OPT_WERROR="--enable-Werror=auto"
                ;;
        esac
    fi

    CONFIG_OPTS+=("CFLAGS=-I${BUILD_PREFIX}/include")
    CONFIG_OPTS+=("CPPFLAGS=-I${BUILD_PREFIX}/include")
    CONFIG_OPTS+=("CXXFLAGS=-I${BUILD_PREFIX}/include")
    CONFIG_OPTS+=("LDFLAGS=-L${BUILD_PREFIX}/lib")
    CONFIG_OPTS+=("PKG_CONFIG_PATH=${BUILD_PREFIX}/lib/pkgconfig")
    CONFIG_OPTS+=("--prefix=${BUILD_PREFIX}")
    if [ -z "${CI_CONFIG_QUIET-}" ] || [ "${CI_CONFIG_QUIET-}" = yes ] || [ "${CI_CONFIG_QUIET-}" = true ]; then
        CONFIG_OPTS+=("--quiet")
    fi

    if [ "$HAVE_CCACHE" = yes ] && [ "${COMPILER_FAMILY}" = GCC ]; then
        PATH="/usr/lib/ccache:$PATH"
        export PATH
        if [ -n "$CC" ] && [ -x "/usr/lib/ccache/`basename "$CC"`" ]; then
            case "$CC" in
                *ccache*) ;;
                */*) DIR_CC="`dirname "$CC"`" && [ -n "$DIR_CC" ] && DIR_CC="`cd "$DIR_CC" && pwd `" && [ -n "$DIR_CC" ] && [ -d "$DIR_CC" ] || DIR_CC=""
                    [ -z "$CCACHE_PATH" ] && CCACHE_PATH="$DIR_CC" || \
                    if echo "$CCACHE_PATH" | egrep '(^'"$DIR_CC"':.*|^'"$DIR_CC"'$|:'"$DIR_CC"':|:'"$DIR_CC"'$)' ; then
                        CCACHE_PATH="$DIR_CC:$CCACHE_PATH"
                    fi
                    ;;
            esac
            CC="/usr/lib/ccache/`basename "$CC"`"
        else
            : # CC="ccache $CC"
        fi
        if [ -n "$CXX" ] && [ -x "/usr/lib/ccache/`basename "$CXX"`" ]; then
            case "$CXX" in
                *ccache*) ;;
                */*) DIR_CXX="`dirname "$CXX"`" && [ -n "$DIR_CXX" ] && DIR_CXX="`cd "$DIR_CXX" && pwd `" && [ -n "$DIR_CXX" ] && [ -d "$DIR_CXX" ] || DIR_CXX=""
                    [ -z "$CCACHE_PATH" ] && CCACHE_PATH="$DIR_CXX" || \
                    if echo "$CCACHE_PATH" | egrep '(^'"$DIR_CXX"':.*|^'"$DIR_CXX"'$|:'"$DIR_CXX"':|:'"$DIR_CXX"'$)' ; then
                        CCACHE_PATH="$DIR_CXX:$CCACHE_PATH"
                    fi
                    ;;
            esac
            CXX="/usr/lib/ccache/`basename "$CXX"`"
        else
            : # CXX="ccache $CXX"
        fi
        if [ -n "$CPP" ] && [ -x "/usr/lib/ccache/`basename "$CPP"`" ]; then
            case "$CPP" in
                *ccache*) ;;
                */*) DIR_CPP="`dirname "$CPP"`" && [ -n "$DIR_CPP" ] && DIR_CPP="`cd "$DIR_CPP" && pwd `" && [ -n "$DIR_CPP" ] && [ -d "$DIR_CPP" ] || DIR_CPP=""
                    [ -z "$CCACHE_PATH" ] && CCACHE_PATH="$DIR_CPP" || \
                    if echo "$CCACHE_PATH" | egrep '(^'"$DIR_CPP"':.*|^'"$DIR_CPP"'$|:'"$DIR_CPP"':|:'"$DIR_CPP"'$)' ; then
                        CCACHE_PATH="$DIR_CPP:$CCACHE_PATH"
                    fi
                    ;;
            esac
            CPP="/usr/lib/ccache/`basename "$CPP"`"
        else
            : # CPP="ccache $CPP"
        fi

        CONFIG_OPTS+=("CC=${CC}")
        CONFIG_OPTS+=("CXX=${CXX}")
        CONFIG_OPTS+=("CPP=${CPP}")
    fi

    CONFIG_OPTS_COMMON=$CONFIG_OPTS
    CONFIG_OPTS+=("--with-docs=no")

    # Clone and build dependencies, if not yet installed to Travis env as DEBs
    # or MacOS packages; other OSes are not currently supported by Travis cloud
    [ -z "$CI_TIME" ] || echo "`date`: Starting build of dependencies (if any)..."

    # Start of recipe for dependency: libsodium
    if ! (command -v dpkg-query >/dev/null 2>&1 && dpkg-query --list libsodium-dev >/dev/null 2>&1) || \
           (command -v brew >/dev/null 2>&1 && brew ls --versions libsodium >/dev/null 2>&1) \
    ; then
        echo ""
        BASE_PWD=${PWD}
        echo "`date`: INFO: Building prerequisite 'libsodium' from Git repository..." >&2
        $CI_TIME git clone --quiet --depth 1 -b release/IPM_Infra-1.4 https://github.com/42ity/libsodium.git libsodium
        cd libsodium
        CCACHE_BASEDIR=${PWD}
        export CCACHE_BASEDIR
        git --no-pager log --oneline -n1
        if [ -e autogen.sh ]; then
            $CI_TIME ./autogen.sh 2> /dev/null
        fi
        if [ -e buildconf ]; then
            $CI_TIME ./buildconf 2> /dev/null
        fi
        if [ ! -e autogen.sh ] && [ ! -e buildconf ] && [ ! -e ./configure ] && [ -s ./configure.ac ]; then
            $CI_TIME libtoolize --copy --force && \
            $CI_TIME aclocal -I . && \
            $CI_TIME autoheader && \
            $CI_TIME automake --add-missing --copy && \
            $CI_TIME autoconf || \
            $CI_TIME autoreconf -fiv
        fi
        $CI_TIME ./configure "${CONFIG_OPTS[@]}"
        $CI_TIME make -j4
        $CI_TIME make install
        cd "${BASE_PWD}"
    fi

    # Start of recipe for dependency: libzmq
    if ! (command -v dpkg-query >/dev/null 2>&1 && dpkg-query --list libzmq3-dev >/dev/null 2>&1) || \
           (command -v brew >/dev/null 2>&1 && brew ls --versions libzmq >/dev/null 2>&1) \
    ; then
        echo ""
        BASE_PWD=${PWD}
        echo "`date`: INFO: Building prerequisite 'libzmq' from Git repository..." >&2
        $CI_TIME git clone --quiet --depth 1 -b release/IPM_Infra-1.4 https://github.com/42ity/libzmq.git libzmq
        cd libzmq
        CCACHE_BASEDIR=${PWD}
        export CCACHE_BASEDIR
        git --no-pager log --oneline -n1
        if [ -e autogen.sh ]; then
            $CI_TIME ./autogen.sh 2> /dev/null
        fi
        if [ -e buildconf ]; then
            $CI_TIME ./buildconf 2> /dev/null
        fi
        if [ ! -e autogen.sh ] && [ ! -e buildconf ] && [ ! -e ./configure ] && [ -s ./configure.ac ]; then
            $CI_TIME libtoolize --copy --force && \
            $CI_TIME aclocal -I . && \
            $CI_TIME autoheader && \
            $CI_TIME automake --add-missing --copy && \
            $CI_TIME autoconf || \
            $CI_TIME autoreconf -fiv
        fi
        $CI_TIME ./configure "${CONFIG_OPTS[@]}"
        $CI_TIME make -j4
        $CI_TIME make install
        cd "${BASE_PWD}"
    fi

    # Start of recipe for dependency: czmq
    if ! (command -v dpkg-query >/dev/null 2>&1 && dpkg-query --list libczmq-dev >/dev/null 2>&1) || \
           (command -v brew >/dev/null 2>&1 && brew ls --versions czmq >/dev/null 2>&1) \
    ; then
        echo ""
        BASE_PWD=${PWD}
        echo "`date`: INFO: Building prerequisite 'czmq' from Git repository..." >&2
        $CI_TIME git clone --quiet --depth 1 -b release/IPM_Infra-1.4 https://github.com/42ity/czmq.git czmq
        cd czmq
        CCACHE_BASEDIR=${PWD}
        export CCACHE_BASEDIR
        git --no-pager log --oneline -n1
        if [ -e autogen.sh ]; then
            $CI_TIME ./autogen.sh 2> /dev/null
        fi
        if [ -e buildconf ]; then
            $CI_TIME ./buildconf 2> /dev/null
        fi
        if [ ! -e autogen.sh ] && [ ! -e buildconf ] && [ ! -e ./configure ] && [ -s ./configure.ac ]; then
            $CI_TIME libtoolize --copy --force && \
            $CI_TIME aclocal -I . && \
            $CI_TIME autoheader && \
            $CI_TIME automake --add-missing --copy && \
            $CI_TIME autoconf || \
            $CI_TIME autoreconf -fiv
        fi
        $CI_TIME ./configure "${CONFIG_OPTS[@]}"
        $CI_TIME make -j4
        $CI_TIME make install
        cd "${BASE_PWD}"
    fi

    # Start of recipe for dependency: malamute
    if ! (command -v dpkg-query >/dev/null 2>&1 && dpkg-query --list libmlm-dev >/dev/null 2>&1) || \
           (command -v brew >/dev/null 2>&1 && brew ls --versions malamute >/dev/null 2>&1) \
    ; then
        echo ""
        BASE_PWD=${PWD}
        echo "`date`: INFO: Building prerequisite 'malamute' from Git repository..." >&2
        $CI_TIME git clone --quiet --depth 1 -b release/IPM_Infra-1.4 https://github.com/42ity/malamute.git malamute
        cd malamute
        CCACHE_BASEDIR=${PWD}
        export CCACHE_BASEDIR
        git --no-pager log --oneline -n1
        if [ -e autogen.sh ]; then
            $CI_TIME ./autogen.sh 2> /dev/null
        fi
        if [ -e buildconf ]; then
            $CI_TIME ./buildconf 2> /dev/null
        fi
        if [ ! -e autogen.sh ] && [ ! -e buildconf ] && [ ! -e ./configure ] && [ -s ./configure.ac ]; then
            $CI_TIME libtoolize --copy --force && \
            $CI_TIME aclocal -I . && \
            $CI_TIME autoheader && \
            $CI_TIME automake --add-missing --copy && \
            $CI_TIME autoconf || \
            $CI_TIME autoreconf -fiv
        fi
        $CI_TIME ./configure "${CONFIG_OPTS[@]}"
        $CI_TIME make -j4
        $CI_TIME make install
        cd "${BASE_PWD}"
    fi

    # Start of recipe for dependency: fty-proto
    if ! (command -v dpkg-query >/dev/null 2>&1 && dpkg-query --list libfty_proto-dev >/dev/null 2>&1) || \
           (command -v brew >/dev/null 2>&1 && brew ls --versions fty-proto >/dev/null 2>&1) \
    ; then
        echo ""
        BASE_PWD=${PWD}
        echo "`date`: INFO: Building prerequisite 'fty-proto' from Git repository..." >&2
        $CI_TIME git clone --quiet --depth 1 -b release/IPM_Infra-1.4 https://github.com/42ity/fty-proto.git fty-proto
        cd fty-proto
        CCACHE_BASEDIR=${PWD}
        export CCACHE_BASEDIR
        git --no-pager log --oneline -n1
        if [ -e autogen.sh ]; then
            $CI_TIME ./autogen.sh 2> /dev/null
        fi
        if [ -e buildconf ]; then
            $CI_TIME ./buildconf 2> /dev/null
        fi
        if [ ! -e autogen.sh ] && [ ! -e buildconf ] && [ ! -e ./configure ] && [ -s ./configure.ac ]; then
            $CI_TIME libtoolize --copy --force && \
            $CI_TIME aclocal -I . && \
            $CI_TIME autoheader && \
            $CI_TIME automake --add-missing --copy && \
            $CI_TIME autoconf || \
            $CI_TIME autoreconf -fiv
        fi
        $CI_TIME ./configure "${CONFIG_OPTS[@]}"
        $CI_TIME make -j4
        $CI_TIME make install
        cd "${BASE_PWD}"
    fi

    # Start of recipe for dependency: cidr
    if ! (command -v dpkg-query >/dev/null 2>&1 && dpkg-query --list libcidr0-dev >/dev/null 2>&1) || \
           (command -v brew >/dev/null 2>&1 && brew ls --versions cidr >/dev/null 2>&1) \
    ; then
        echo ""
        BASE_PWD=${PWD}
        echo "`date`: INFO: Building prerequisite 'cidr' from Git repository..." >&2
        $CI_TIME git clone --quiet --depth 1 -b release/IPM_Infra-1.4 https://github.com/42ity/libcidr.git cidr
        cd cidr
        CCACHE_BASEDIR=${PWD}
        export CCACHE_BASEDIR
        git --no-pager log --oneline -n1
        if [ -e autogen.sh ]; then
            $CI_TIME ./autogen.sh 2> /dev/null
        fi
        if [ -e buildconf ]; then
            $CI_TIME ./buildconf 2> /dev/null
        fi
        if [ ! -e autogen.sh ] && [ ! -e buildconf ] && [ ! -e ./configure ] && [ -s ./configure.ac ]; then
            $CI_TIME libtoolize --copy --force && \
            $CI_TIME aclocal -I . && \
            $CI_TIME autoheader && \
            $CI_TIME automake --add-missing --copy && \
            $CI_TIME autoconf || \
            $CI_TIME autoreconf -fiv
        fi
        $CI_TIME ./configure "${CONFIG_OPTS[@]}"
        $CI_TIME make -j4
        $CI_TIME make install
        cd "${BASE_PWD}"
    fi

    # Start of recipe for dependency: cxxtools
    if ! (command -v dpkg-query >/dev/null 2>&1 && dpkg-query --list cxxtools-dev >/dev/null 2>&1) || \
           (command -v brew >/dev/null 2>&1 && brew ls --versions cxxtools >/dev/null 2>&1) \
    ; then
        echo ""
        BASE_PWD=${PWD}
        echo "`date`: INFO: Building prerequisite 'cxxtools' from Git repository..." >&2
        $CI_TIME git clone --quiet --depth 1 -b release/IPM_Infra-1.4 https://github.com/42ity/cxxtools.git cxxtools
        cd cxxtools
        CCACHE_BASEDIR=${PWD}
        export CCACHE_BASEDIR
        git --no-pager log --oneline -n1
        if [ -e autogen.sh ]; then
            $CI_TIME ./autogen.sh 2> /dev/null
        fi
        if [ -e buildconf ]; then
            $CI_TIME ./buildconf 2> /dev/null
        fi
        if [ ! -e autogen.sh ] && [ ! -e buildconf ] && [ ! -e ./configure ] && [ -s ./configure.ac ]; then
            $CI_TIME libtoolize --copy --force && \
            $CI_TIME aclocal -I . && \
            $CI_TIME autoheader && \
            $CI_TIME automake --add-missing --copy && \
            $CI_TIME autoconf || \
            $CI_TIME autoreconf -fiv
        fi
        $CI_TIME ./configure "${CONFIG_OPTS[@]}"
        $CI_TIME make -j4
        $CI_TIME make install
        cd "${BASE_PWD}"
    fi

    # Start of recipe for dependency: libnutclient
    if ! (command -v dpkg-query >/dev/null 2>&1 && dpkg-query --list libnutclient-dev >/dev/null 2>&1) || \
           (command -v brew >/dev/null 2>&1 && brew ls --versions libnutclient >/dev/null 2>&1) \
    ; then
        echo ""
        BASE_PWD=${PWD}
        echo "`date`: INFO: Building prerequisite 'libnutclient' from Git repository..." >&2
        $CI_TIME git clone --quiet --depth 1 -b release/IPM_Infra-1.4 https://github.com/42ity/nut.git libnutclient
        cd libnutclient
        CCACHE_BASEDIR=${PWD}
        export CCACHE_BASEDIR
        git --no-pager log --oneline -n1
        if [ -e autogen.sh ]; then
            $CI_TIME ./autogen.sh 2> /dev/null
        fi
        if [ -e buildconf ]; then
            $CI_TIME ./buildconf 2> /dev/null
        fi
        if [ ! -e autogen.sh ] && [ ! -e buildconf ] && [ ! -e ./configure ] && [ -s ./configure.ac ]; then
            $CI_TIME libtoolize --copy --force && \
            $CI_TIME aclocal -I . && \
            $CI_TIME autoheader && \
            $CI_TIME automake --add-missing --copy && \
            $CI_TIME autoconf || \
            $CI_TIME autoreconf -fiv
        fi
        ( # Custom additional options for libnutclient
            CONFIG_OPTS+=("--with-doc=no")
            CONFIG_OPTS+=("--with-all=no")
            CONFIG_OPTS+=("--with-dev=yes")
            CONFIG_OPTS+=("--with-dmfnutscan-regenerate=no")
            CONFIG_OPTS+=("--with-dmfsnmp-regenerate=no")
            $CI_TIME ./configure "${CONFIG_OPTS[@]}"
        )
        $CI_TIME make -j4
        $CI_TIME make install
        cd "${BASE_PWD}"
    fi

    # Build and check this project; note that zprojects always have an autogen.sh
    echo ""
    echo "`date`: INFO: Starting build of currently tested project with DRAFT APIs..."
    CCACHE_BASEDIR=${PWD}
    export CCACHE_BASEDIR
    if [ "$BUILD_TYPE" = "default-with-docs" ]; then
        CONFIG_OPTS=$CONFIG_OPTS_COMMON
        CONFIG_OPTS+=("--with-docs=yes")
    fi
    if [ -n "$ADDRESS_SANITIZER" ] && [ "$ADDRESS_SANITIZER" == "enabled" ]; then
        CONFIG_OPTS+=("--enable-address-sanitizer=yes")
    fi
    # Only use --enable-Werror on projects that are expected to have it
    # (and it is not our duty to check prerequisite projects anyway)
    CONFIG_OPTS+=("${CONFIG_OPT_WERROR}")
    $CI_TIME ./autogen.sh 2> /dev/null
    $CI_TIME ./configure --enable-drafts=yes "${CONFIG_OPTS[@]}"
    case "$BUILD_TYPE" in
        valgrind)
            # Build and check this project
            $CI_TIME make VERBOSE=1 memcheck && exit
            echo "Re-running failed ($?) memcheck with greater verbosity" >&2
            $CI_TIME make VERBOSE=1 memcheck-verbose
            exit $?
            ;;
        clang-format-check)
            $CI_TIME make VERBOSE=1 clang-format-check-CI
            exit $?
            ;;
    esac
    $CI_TIME make VERBOSE=1 all

    echo "=== Are GitIgnores good after 'make all' with drafts?"
    make check-gitignore
    echo "==="

    (
        export DISTCHECK_CONFIGURE_FLAGS="--enable-drafts=yes ${CONFIG_OPTS[@]}"
        $CI_TIME make VERBOSE=1 DISTCHECK_CONFIGURE_FLAGS="$DISTCHECK_CONFIGURE_FLAGS" distcheck

        echo "=== Are GitIgnores good after 'make distcheck' with drafts?"
        make check-gitignore
        echo "==="
    )

    # Build and check this project without DRAFT APIs
    echo ""
    echo "`date`: INFO: Starting build of currently tested project without DRAFT APIs..."
    make distclean

    git clean -f
    git reset --hard HEAD
    (
        $CI_TIME ./autogen.sh 2> /dev/null
        $CI_TIME ./configure --enable-drafts=no "${CONFIG_OPTS[@]}"
        $CI_TIME make VERBOSE=1 all || exit $?
        (
            export DISTCHECK_CONFIGURE_FLAGS="--enable-drafts=no ${CONFIG_OPTS[@]}" && \
            $CI_TIME make VERBOSE=1 DISTCHECK_CONFIGURE_FLAGS="$DISTCHECK_CONFIGURE_FLAGS" distcheck || exit $?
        )
    ) || exit 1
    [ -z "$CI_TIME" ] || echo "`date`: Builds completed without fatal errors!"

    echo "=== Are GitIgnores good after 'make distcheck' without drafts?"
    make check-gitignore
    echo "==="

    if [ "$HAVE_CCACHE" = yes ]; then
        echo "CCache stats after build:"
        ccache -s
    fi
    ;;
bindings)
    pushd "./bindings/${BINDING}" && ./ci_build.sh
    ;;
*)
    pushd "./builds/${BUILD_TYPE}" && REPO_DIR="$(dirs -l +1)" ./ci_build.sh
    ;;
esac
