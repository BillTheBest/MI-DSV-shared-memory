
#!/bin/bash

if [ ! -e ./nodeno ]; then
  read -p "Zadejte číslo uzlu (1,254):" nodeno
  echo $nodeno > ./nodeno
else
  nodeno=$(cat ./nodeno)
fi

case $nodeno in
  1) ./dsvsm -1 -l 1234 ;;
  *) ./dsvsm -l 1234 -t node1 -p 1234 ;;
esac

