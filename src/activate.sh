# Bash startup file embedded by ttrek activate.
if [[ -r ${HOME:-}/.bashrc ]]; then
    source "$HOME/.bashrc"
fi

_ttrek_activate() {
    _TTREK_OLD_NAMES=(PATH LD_LIBRARY_PATH PROJECT_HOME PROJECT_INSTALL_DIR
                      TTREK_VENV TTREK_VENV_PROMPT PS1)
    _TTREK_OLD_VALUES=()
    _TTREK_OLD_SET=()
    local name
    for name in "${_TTREK_OLD_NAMES[@]}"; do
        _TTREK_OLD_VALUES+=("${!name-}")
        _TTREK_OLD_SET+=("${!name+x}")
    done

    export PATH="$1" LD_LIBRARY_PATH="$2" PROJECT_HOME="$3"
    export PROJECT_INSTALL_DIR="$4" TTREK_VENV="$5" TTREK_VENV_PROMPT=ttrek
    if [[ -z ${TTREK_VENV_DISABLE_PROMPT:-} ]]; then
        PS1="(ttrek) ${PS1:-}"
        export PS1
    fi
    hash -r
}

deactivate() {
    local i name
    for ((i = 0; i < ${#_TTREK_OLD_NAMES[@]}; i++)); do
        name=${_TTREK_OLD_NAMES[i]}
        if [[ ${_TTREK_OLD_SET[i]} ]]; then
            printf -v "$name" '%s' "${_TTREK_OLD_VALUES[i]}"
            export "$name"
        else
            unset "$name"
        fi
    done
    unset _TTREK_OLD_NAMES _TTREK_OLD_VALUES _TTREK_OLD_SET
    hash -r
    unset -f deactivate
}
