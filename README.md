# Introduce

tbox is a ddns, include server and client side, client size get home host's ipv4 and ipv6 address 
then call server to change aws route53 to change dns record point to client's newest ip;

## feature
tbox written in C++, so with minimal requirement for deployment, can deploy linux, macos, windows, android, ios, x86_64, arm ...


```
sudo systemctl daemon-reload
sudo systemctl enable tbox_server.service
sudo systemctl start tbox_server.service

```
