Client Side concurrent + persistent proxy.
It uses [kqueue](https://en.wikipedia.org/wiki/Kqueue) so it works for macOs.
Does not handle well fragmented requests.
Does not handle netcat either. (I gave it a quick shot but don't care much about it)
