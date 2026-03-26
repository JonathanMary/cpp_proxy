Client Side concurrent + persistent proxy.
It uses [kqueue](https://en.wikipedia.org/wiki/Kqueue) so it works for macOs.
Quite brittle, that's a learning project.
Does not handle fragmented requests very well.
Does not handle netcat either. (I gave it a quick shot but don't care much about it)
