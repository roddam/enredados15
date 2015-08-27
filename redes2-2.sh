#!/bin/bash

if test $# -ne 1
then
  echo 'Uso: ejercicio2-2.sh host1'
  exit
fi

umbral="10.0"
host=$1

trace=$(traceroute $host | sed '1d')
cont=1

ultPaso=0;

while IFS= read -r line; do

	tiempos=$(echo $line | grep "[0-9]\{1,\}\.[0-9]\{1,\} ms" -o | grep "[0-9]\{1,\}\.[0-9]\{1,\}" -o)
	cont2=0
	suma=0

	while IFS= read -r tiempo; do
		if [ ! -z "$tiempo" ]; then
			#printf '%s\n' "${tiempo}"
			let cont2=cont2+1
			suma=$(echo "scale=3;$suma + $tiempo" | bc)
		fi
	done <<< "$tiempos"

	condicion=$(echo "$suma > 0" | bc)

	if [ $condicion = '1' ]; then
		promedio=$(echo "scale=3; $suma/$cont2" | bc)
		salto=$(echo "scale=3; $promedio - $ultPaso" | bc)
		
		#echo "El ultimo paso fue de $ultPaso"		
		#echo "El salto fue de $salto"
		condUmbral=$(echo "$salto > $umbral" | bc)

		if [ $condUmbral = '1' ]; then
			echo "En el hop $cont se supero el umbral" 
		fi

		ultPaso=$promedio
		#echo "El promdio de tiempo es $promedio"		
	fi

	#echo "Salto: $cont"
	let cont=cont+1
#	tiempo=$(echo $line | grep "[0-9]\{1,\}\.[0-9]\{1,\} ms" -o )
	
done <<< "$trace"





