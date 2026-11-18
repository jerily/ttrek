# Zsh startup files have already been read by the ZDOTDIR wrapper.
function _ttrek_activate {
    emulate -L zsh
    typeset -ga _TTREK_OLD_NAMES=(PATH LD_LIBRARY_PATH PROJECT_HOME PROJECT_INSTALL_DIR
                                TTREK_VENV TTREK_VENV_PROMPT PS1)
    typeset -gA _TTREK_OLD_VALUES=() _TTREK_OLD_SET=()
    local name
    for name in "${_TTREK_OLD_NAMES[@]}"; do
        _TTREK_OLD_SET[$name]=${(P)+name}
        _TTREK_OLD_VALUES[$name]=${(P)name-}
    done

    export PATH="$1" LD_LIBRARY_PATH="$2" PROJECT_HOME="$3"
    export PROJECT_INSTALL_DIR="$4" TTREK_VENV="$5" TTREK_VENV_PROMPT=ttrek
    if [[ -z ${TTREK_VENV_DISABLE_PROMPT:-} ]]; then
        export PS1="(ttrek) ${PS1:-}"
    fi
    rehash
}

function deactivate {
    emulate -L zsh
    local name
    for name in "${_TTREK_OLD_NAMES[@]}"; do
        if (( _TTREK_OLD_SET[$name] )); then
            export "$name=${_TTREK_OLD_VALUES[$name]}"
        else
            unset "$name"
        fi
    done
    unset _TTREK_OLD_NAMES _TTREK_OLD_VALUES _TTREK_OLD_SET
    rehash
    unfunction deactivate
}
