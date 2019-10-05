import sys
folder_name=sys.argv[1] #"test"
prefix="del"
MAX_DIST=10001

number_of_threads=0
with open(folder_name+"/"+prefix, "r") as readfile:
    for line in readfile:
        number_of_threads=int((line.split())[-1])
        break
if number_of_threads == 0:
    sys.exit()

count=[]
for i in range(MAX_DIST+2):
    count.append(0)

total_ins_count=0

for i in range(number_of_threads):
    with open(folder_name+"/"+prefix+`i`,"r") as readfile:
        isFirst=True
        for line in readfile:
            if isFirst:
                total_ins_count+=int( (line.split())[-1] )
                isFirst=False
            else:
                pair=line.split(",")
                index,value=int(pair[0]),int(pair[1])
                count[index]+=value
total_miss_count=0
for i in range(MAX_DIST+2):
    total_miss_count+=count[i]
running_count=0
print total_miss_count, total_ins_count, (1000.0*total_miss_count)/total_ins_count
for i in range(MAX_DIST+2):
    running_count+=count[i]
    if count[i]!=0:
        print i,(1.0*running_count/total_miss_count),count[i]
