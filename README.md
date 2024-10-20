# Introduce

tbox is a ddns, include server and client side, client side obtain home host newest ipv4 and ipv6 address, 
then call server to change dns record point to client's newest ip. currently only support aws route53

## feature
tbox written in C++, so with minimal requirement for deployment, can deploy linux, macos, windows, android, ios, x86_64, arm ...


```
sudo systemctl daemon-reload
sudo systemctl enable tbox_server.service
sudo systemctl start tbox_server.service

```
