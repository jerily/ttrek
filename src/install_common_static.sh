
if [ "$1" = "-v" ] || [ "$1" = "-fail-verbose" ]; then
    TTREK_FAIL_VERBOSE=1
    shift
fi

init_tty() {
    [ "$IS_TTY" != '0' ] || return 0
    [ -z "$IS_TTY" ] && [ ! -t 1 ] && { IS_TTY=0; return 0; } || IS_TTY=0
    COLUMNS="$(tput cols 2>/dev/null || true)"
    case "$COLUMNS" in ''|*[!0-9]*) return;; esac
    BAR_LEN=$(( COLUMNS - 2 - 6 ))
    IS_TTY=1
    _G="$(tput setaf 2)"
    _R="$(tput setaf 9)"
    _A="$(tput setaf 8)"
    _T="$(tput sgr0)"
    echo
}

progress() {
    [ $IS_TTY -eq 1 ] || return 0
    printf '\033[1B\r\033[K'
    if [ $# -eq 2 ]; then
        PERCENT=$(( $1 * 100 / $2 ))
        BAR_LEN_CUR=$(( $1 * BAR_LEN / $2 ))
        printf '['
        [ "$BAR_LEN_CUR" -eq 0 ] || printf '=\033['$(( BAR_LEN_CUR - 1 ))';b'
        BAR_LEN_CUR=$(( BAR_LEN - BAR_LEN_CUR ))
        [ "$BAR_LEN_CUR" -eq 0 ] || printf -- '-\033['$(( BAR_LEN_CUR - 1 ))';b'
        printf '] %3s%%' "$PERCENT"
    fi
    [ $# -eq 1 ] || printf '\033[1A\r\033[K'
}

stage() {
    [ "$STAGE" != "$1" ] || return 0
    STAGE="$1"
    if [ "$STAGE" = ok ]; then
        progress
        STAGE_MSG=": ${_G}Done.${_T}"
        STAGE=5
    elif [ "$STAGE" = fail ]; then
        STAGE_MSG="${_R}Fail.${_T}"
        if [ $IS_TTY -eq 1 ]; then
            printf "\033[3D - $STAGE_MSG"
            unset STAGE_MSG
            progress -
        else
            STAGE_MSG=": $STAGE_MSG"
            progress
        fi
        STAGE=5
    else
        STAGE_MSG=" [${STAGE}/4]:"
        [ "$STAGE" != 1 ] || STAGE_MSG="$STAGE_MSG Getting sources..."
        [ "$STAGE" != 2 ] || STAGE_MSG="$STAGE_MSG Configuring sources..."
        [ "$STAGE" != 3 ] || STAGE_MSG="$STAGE_MSG Building..."
        [ "$STAGE" != 4 ] || STAGE_MSG="$STAGE_MSG Installing..."
        STAGE_TOT=$(( PKG_TOT * 5 ))
        STAGE_CUR=$(( (PKG_CUR - 1) * 5 + STAGE ))
        progress "$STAGE_CUR" "$STAGE_TOT"
    fi
    [ -z "$STAGE_MSG" ] || printf "Package [%s/%s]: %s v%s${_A};${_T} Stage%s" "$PKG_CUR" "$PKG_TOT" "$PACKAGE" "$VERSION" "$STAGE_MSG"
    if [ "$STAGE" = 5 ] || [ $IS_TTY -eq 0 ]; then echo; fi
    [ -z "$2" ] || exit "$2"
}

ok() { stage ok; }
fail() {
    R=$?
    stage fail
    echo "${_R}Failed command${_A}:${_T} $LATEST_COMMAND"
    if [ -n "$1" ] && [ -e "$1" ]; then
        if [ -z "$TTREK_FAIL_VERBOSE" ]; then
            echo "${_R}Check the details in the log file${_A}:${_T} $1"
        else
            echo "${_R}Log file${_A}:${_T} $1"
            cat "$1"
        fi
    fi
    echo
    exit $R
}
cmd() {
    LATEST_COMMAND="$@"
    "$@"
}

init_tty
