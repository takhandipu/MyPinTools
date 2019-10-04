tmp_name="mine.txt"
iter_count=2
rm -rf $tmp_name
for((iter=0;iter<iter_count;iter++))
do
 $PIN_ROOT/pin -t obj-intel64/prefetch.so -- ./a.out 2>> $tmp_name
done
awk -F, 'NR%2==0{c++;for(i = 2; i <= NF; i++){x[i]+=$i;y[i]+=$i^2}}END{for(i in x)printf "%0.2f,", x[i]/c;printf "stddev,";for(i in x)printf "%0.2f,", sqrt(y[i]/c-(x[i]/c)^2); printf "\n"}' $tmp_name
