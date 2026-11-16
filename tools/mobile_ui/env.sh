# Source this with your own values set first:
#   export TK_NAME=inv            (short unique name)
#   export TK_DISP=98             (X display number, unique per agent: 96, 97, 98 ...)
#   export TK_GAME=/path/to/your/worktree
KIT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
export TK_WORK=/home/user/tk-$TK_NAME
mkdir -p $TK_WORK/shots $TK_WORK/user/config
[ -f $TK_WORK/user/config/options.json ] || cp $KIT/options.json $TK_WORK/user/config/options.json
