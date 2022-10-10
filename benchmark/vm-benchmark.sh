#/bin/sh

player=a2play

echo $player
usage()
{
cat << EOF
usage: $0 options

OPTIONS:
   -h      Show this message
   -v      Verbose

EOF
}

echo ===== Audiality 2 offline rendering VM benchmark =====
echo

VERBOSE=
while getopts "hv" OPTION
do
   case $OPTION in
      h)
         usage
         exit 1
         ;;
      v)
         VERBOSE=1
         ;;
      ?)
         usage
         exit
         ;;
   esac
done

${player} -v

echo ===================================================

for SONGNAME in $(ls *.a2s)
do
   echo
   echo === $SONGNAME ===
   for i in {1..3}
   do
      echo Pass $i
      if [ ! -z $VERBOSE ]; then
         time ${player} -dbuffer -r1000 $SONGNAME -pSong -st5000
      else
         time $(${player} -dbuffer -r1000 $SONGNAME -pSong -st5000 > /dev/null 2>&1)
      fi
      echo
   done
done

echo ===================================================
