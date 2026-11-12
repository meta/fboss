# bash completion for the fboss2 / fboss2-dev / fboss2-readonly CLIs (OSS).
#
# The binaries expose their compiled-in command tree via the hidden
# `__completion` subcommand: given the words typed so far (the last one being
# the word under the cursor), they print the candidate completions one per
# line: sub-commands, options, and the leaf attribute tokens of commands that
# implement completeArgs (see fboss/cli/fboss2/utils/ArgCompletion.h). Because
# the answer comes from the binary itself, completion never goes stale as
# commands are added.
#
# Needs only bash builtins; the bash-completion package is optional.
# Install: source it from your shell rc, or copy it to /etc/bash_completion.d/.
_fboss2_complete() {
  local line cur completions
  local -a words
  # Readline splits COMP_WORDS on COMP_WORDBREAKS, which includes ':' and '=',
  # so an IPv6 address or `--host=sw1` would arrive as several fragments and
  # be miscounted as positionals. Re-split the line up to the cursor on
  # whitespace only instead. A trailing space means the cursor is on a new,
  # empty word.
  line=${COMP_LINE:0:COMP_POINT}
  read -ra words <<<"$line"
  if [[ ${#words[@]} -eq 0 || $line == *[[:space:]] ]]; then
    words+=("")
  fi
  cur=${words[-1]}
  # Ask the binary being completed so fboss2, fboss2-dev and fboss2-readonly
  # each describe their own command tree.
  completions="$("${words[0]}" __completion "${words[@]:1}" 2>/dev/null)"
  # shellcheck disable=SC2207
  COMPREPLY=($(compgen -W "${completions}" -- "${cur}"))
}
complete -F _fboss2_complete fboss2 fboss2-dev fboss2-readonly
