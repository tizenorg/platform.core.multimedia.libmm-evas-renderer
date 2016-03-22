aclocal
libtoolize --copy -f
autoheader
autoconf
automake --add-missing --copy --foreign
