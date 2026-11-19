#!/bin/bash
# Print the Configure options trunk/auto/depends.sh passes to openssl-4-fit for --ssl-4=on: the common
# OPENSSL_OPTIONS, then every OPENSSL_OPTIONS line of the SRS_SSL_4 branch.
# Usage: options.sh <trunk/auto/depends.sh>
set -u
DEPENDS=${1:?usage: options.sh <trunk/auto/depends.sh>}
awk '
    /^    OPENSSL_OPTIONS="-no-shared/ { sub(/^ *OPENSSL_OPTIONS="/, ""); sub(/"$/, ""); base = $0 }
    /elif \[\[ \$SRS_SSL_4 == YES \]\]/ { in4 = 1; next }
    in4 && /^    (elif|else)/ { in4 = 0 }
    in4 && /OPENSSL_OPTIONS="\$OPENSSL_OPTIONS / { sub(/^ *OPENSSL_OPTIONS="\$OPENSSL_OPTIONS /, ""); sub(/"$/, ""); cut = cut " " $0 }
    END { if (base == "" || cut == "") exit 1; print base cut }
' "$DEPENDS"
