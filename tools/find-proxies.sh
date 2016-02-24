#!/bin/bash
#Crude way of finding proxies for testing stuff.


curl -A Mozilla -s 'http://www.socks-proxy.net/' |  awk -F "</*td>|</*tr>" '/<\/*t[rd]>.*[A-Z][A-Z]/ {print $3":"$5}'
curl -A Mozilla -s 'http://www.gatherproxy.com/sockslist' | awk -F"<script>|</script>" '/document.write\(.*/ {print $2}' | tr -d ' ' | sed 's/document\.write(\x27//g ; s/\x27)//g' | paste -d":" - -
for i in {1..5}; do
        curl -s "http://www.samair.ru/proxy/socks0${i}.htm" | egrep -o '[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}:[0-9]{1,5}'
done
curl -A Mozilla -s "http://www.my-proxy.com/free-socks-4-proxy.html" | egrep -o '[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}:[0-9]{1,5}'
curl -A Mozilla -s "http://www.my-proxy.com/free-socks-5-proxy.html" | egrep -o '[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}:[0-9]{1,5}'
for i in {1..20}; do
curl -s "http://www.xroxy.com/proxylist.php?port=&type=All_socks&ssl=&country=&latency=&reliability=&sort=reliability&desc=true&pnum=${i}" -A Mozilla | grep 'proxy:name' | awk -F"host=|&port=|&isSocks" '{print $2":"$3}'
done

