# V2 Benchmark Results

```
$ make clean && make
rm -f sender receiver
c++ -O2 -Wall -std=c++17 -pthread -o sender sender.cpp
c++ -O2 -Wall -std=c++17 -pthread -o receiver receiver.cpp

$ python3 run.py --profile profiles/A.json --delay_ms 70
endpoints done
relay done: {'up_bytes': 479120, 'down_bytes': 0, 'up_pkts': 1500, 'down_pkts': 0, 'dropped': 34, 'duplicated': 10}
================ SCORE ================
  frames               : 1500
  deadline misses      : 14  (0.93%)   [cap 1.00%]
  playout delay        : 70 ms   <-- your score if valid; lower wins
  bandwidth overhead   : 2.00x   [cap 2.00x]   (up 479120B, feedback 0B)
  RESULT               : VALID

$ python3 run.py --profile profiles/A.json --delay_ms 65
endpoints done
relay done: {'up_bytes': 479120, 'down_bytes': 0, 'up_pkts': 1500, 'down_pkts': 0, 'dropped': 34, 'duplicated': 10}
================ SCORE ================
  frames               : 1500
  deadline misses      : 18  (1.20%)   [cap 1.00%]
  playout delay        : 65 ms   <-- your score if valid; lower wins
  bandwidth overhead   : 2.00x   [cap 2.00x]   (up 479120B, feedback 0B)
  RESULT               : INVALID
  (reduce misses under 1% and overhead under 2x, THEN minimize delay)

$ python3 run.py --profile profiles/A.json --delay_ms 60
endpoints done
relay done: {'up_bytes': 479120, 'down_bytes': 0, 'up_pkts': 1500, 'down_pkts': 0, 'dropped': 34, 'duplicated': 10}
================ SCORE ================
  frames               : 1500
  deadline misses      : 67  (4.47%)   [cap 1.00%]
  playout delay        : 60 ms   <-- your score if valid; lower wins
  bandwidth overhead   : 2.00x   [cap 2.00x]   (up 479120B, feedback 0B)
  RESULT               : INVALID
  (reduce misses under 1% and overhead under 2x, THEN minimize delay)

$ python3 run.py --profile profiles/B.json --delay_ms 120
endpoints done
relay done: {'up_bytes': 479120, 'down_bytes': 0, 'up_pkts': 1500, 'down_pkts': 0, 'dropped': 81, 'duplicated': 17}
================ SCORE ================
  frames               : 1500
  deadline misses      : 13  (0.87%)   [cap 1.00%]
  playout delay        : 120 ms   <-- your score if valid; lower wins
  bandwidth overhead   : 2.00x   [cap 2.00x]   (up 479120B, feedback 0B)
  RESULT               : VALID

$ python3 run.py --profile profiles/B.json --delay_ms 110
endpoints done
relay done: {'up_bytes': 479120, 'down_bytes': 0, 'up_pkts': 1500, 'down_pkts': 0, 'dropped': 81, 'duplicated': 17}
================ SCORE ================
  frames               : 1500
  deadline misses      : 20  (1.33%)   [cap 1.00%]
  playout delay        : 110 ms   <-- your score if valid; lower wins
  bandwidth overhead   : 2.00x   [cap 2.00x]   (up 479120B, feedback 0B)
  RESULT               : INVALID
  (reduce misses under 1% and overhead under 2x, THEN minimize delay)
```
