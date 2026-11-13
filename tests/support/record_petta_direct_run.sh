#!/bin/sh
# record_petta_direct_run.sh CORPUS_DIR COMMAND... -- [OPTIONS] FILE.metta...
#
# Stands in for the command a gate runs a PeTTa program with.  The .metta files
# of the run, in the order given, are kept as one program in CORPUS_DIR (named
# by the last file and a digest of the whole, so the same run is kept once),
# each file under a line "; recorded-from DIRECTORY" that says where it stood;
# then the run goes on as given.  The gate's verdict is unchanged.
set -eu
corpus=$1
shift
command=
while [ "$#" -gt 0 ] && [ "$1" != "--" ]; do
    command="$command $1"
    shift
done
shift
last=
whole=$(mktemp "$corpus/.run.XXXXXX")
for argument in "$@"; do
    case "$argument" in
        *.metta)
            if [ -f "$argument" ]; then
                # where the file stood: a name it imports is read from there
                printf '; recorded-from %s\n' "$(cd "$(dirname "$argument")" && pwd)" >> "$whole"
                cat "$argument" >> "$whole"
                printf '\n' >> "$whole"
                last=$argument
            fi ;;
    esac
done
if [ -n "$last" ]; then
    digest=$(sha256sum "$whole" | cut -c1-10)
    name=$(basename "$last" .metta)
    mv "$whole" "$corpus/$name.$digest.metta"
else
    rm -f "$whole"
fi
exec $command "$@"
