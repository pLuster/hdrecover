aclocal $AL_OPTS
autoconf $AC_OPTS
autoheader $AH_OPTS
automake --add-missing $AM_OPTS

echo
echo "Now run ./configure and make"
echo
