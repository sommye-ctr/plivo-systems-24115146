# V1 Benchmark Results

```
$ make clean && make
rm -f sender receiver
c++ -O2 -Wall -std=c++17 -pthread -o sender sender.cpp
c++ -O2 -Wall -std=c++17 -pthread -o receiver receiver.cpp

$ python3 run.py --profile profiles/A.json --delay_ms 60
endpoints done
relay done: {'up_bytes': 427180, 'down_bytes': 0, 'up_pkts': 1500, 'down_pkts': 0, 'dropped': 34, 'duplicated': 10}
================ SCORE ================
  frames               : 1500
  deadline misses      : 95  (6.33%)   [cap 1.00%]
  playout delay        : 60 ms   <-- your score if valid; lower wins
  bandwidth overhead   : 1.78x   [cap 2.00x]   (up 427180B, feedback 0B)
  RESULT               : INVALID
  (reduce misses under 1% and overhead under 2x, THEN minimize delay)

$ python3 run.py --profile profiles/A.json --delay_ms 50
endpoints done
relay done: {'up_bytes': 427180, 'down_bytes': 0, 'up_pkts': 1500, 'down_pkts': 0, 'dropped': 34, 'duplicated': 10}
================ SCORE ================
  frames               : 1500
  deadline misses      : 121  (8.07%)   [cap 1.00%]
  playout delay        : 50 ms   <-- your score if valid; lower wins
  bandwidth overhead   : 1.78x   [cap 2.00x]   (up 427180B, feedback 0B)
  RESULT               : INVALID
  (reduce misses under 1% and overhead under 2x, THEN minimize delay)

$ python3 run.py --profile profiles/A.json --delay_ms 40
endpoints done
relay done: {'up_bytes': 427180, 'down_bytes': 0, 'up_pkts': 1500, 'down_pkts': 0, 'dropped': 34, 'duplicated': 10}
================ SCORE ================
  frames               : 1500
  deadline misses      : 523  (34.87%)   [cap 1.00%]
  playout delay        : 40 ms   <-- your score if valid; lower wins
  bandwidth overhead   : 1.78x   [cap 2.00x]   (up 427180B, feedback 0B)
  RESULT               : INVALID
  (reduce misses under 1% and overhead under 2x, THEN minimize delay)

$ python3 run.py --profile profiles/B.json --delay_ms 100
endpoints done
relay done: {'up_bytes': 427180, 'down_bytes': 0, 'up_pkts': 1500, 'down_pkts': 0, 'dropped': 81, 'duplicated': 17}
================ SCORE ================
  frames               : 1500
  deadline misses      : 165  (11.00%)   [cap 1.00%]
  playout delay        : 100 ms   <-- your score if valid; lower wins
  bandwidth overhead   : 1.78x   [cap 2.00x]   (up 427180B, feedback 0B)
  RESULT               : INVALID
  (reduce misses under 1% and overhead under 2x, THEN minimize delay)

$ python3 run.py --profile profiles/B.json --delay_ms 80
endpoints done
relay done: {'up_bytes': 427180, 'down_bytes': 0, 'up_pkts': 1500, 'down_pkts': 0, 'dropped': 81, 'duplicated': 17}
================ SCORE ================
  frames               : 1500
  deadline misses      : 374  (24.93%)   [cap 1.00%]
  playout delay        : 80 ms   <-- your score if valid; lower wins
  bandwidth overhead   : 1.78x   [cap 2.00x]   (up 427180B, feedback 0B)
  RESULT               : INVALID
  (reduce misses under 1% and overhead under 2x, THEN minimize delay)

$ python3 run.py --profile profiles/B.json --delay_ms 60
endpoints done
relay done: {'up_bytes': 427180, 'down_bytes': 0, 'up_pkts': 1500, 'down_pkts': 0, 'dropped': 81, 'duplicated': 17}
================ SCORE ================
  frames               : 1500
  deadline misses      : 833  (55.53%)   [cap 1.00%]
  playout delay        : 60 ms   <-- your score if valid; lower wins
  bandwidth overhead   : 1.78x   [cap 2.00x]   (up 427180B, feedback 0B)
  RESULT               : INVALID
  (reduce misses under 1% and overhead under 2x, THEN minimize delay)
```
