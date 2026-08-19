#!/bin/sh

set -x

if [ ! -d lang/po ]
then
    if [ -d ../lang/po ]
    then
        cd ..
    else
        echo "Error: Could not find lang/po subdirectory."
        exit 1
    fi
fi

# Check output dir here
# Backward compatibility
if [ -z $LOCALE_DIR ]
then
    LOCALE_DIR="lang/mo"
fi

compile_lang()
{
    n="$1"
    f="lang/po/${n}.po"
    override="lang/po_overrides/${n}.po"
    mkdir -p "$LOCALE_DIR/${n}/LC_MESSAGES"
    if [ -f "$override" ]
    then
        msgcat --use-first "$override" "$f" | msgfmt -f -o "$LOCALE_DIR/${n}/LC_MESSAGES/cataclysm-dda.mo" -
    else
        msgfmt -f -o "$LOCALE_DIR/${n}/LC_MESSAGES/cataclysm-dda.mo" "$f"
    fi
}

# compile .mo file for each specified language
if [ $# -gt 0 ] && [ $1 != "all" ]
then
    for n in $@
    do
        compile_lang "$n"
    done
else
    # if nothing specified, compile .mo file for every .po file in lang/po
    for f in lang/po/*.po
    do
        n=`basename $f .po`
        compile_lang "$n"
    done
fi
