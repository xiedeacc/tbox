# DDNS Client - Quick Start

## 🎉 Build Complete!

The dynamic DNS client for IPv6 has been successfully built and is ready for deployment.

## 📦 What's Been Done

✅ IPv6 detection using Linux network APIs
✅ DNS resolution checking with cache bypass  
✅ Route53 record updates via AWS SDK
✅ Multiple IPv6 address handling
✅ Systemd service integration
✅ Complete documentation

## 🚀 Quick Deploy (When SSH is Available)

### Option 1: Automated Deployment Script
```bash
cd /home/ubuntu/src/cpp/tbox/deploy/ddns_client
./deploy_to_server.sh [YOUR_AWS_KEY] [YOUR_AWS_SECRET]
```

### Option 2: Manual Steps
```bash
# 1. Copy to server
scp -6 -r /home/ubuntu/src/cpp/tbox/deploy/ddns_client root@home.xiedeacc.com:/root/

# 2. SSH and install
ssh -6 root@home.xiedeacc.com
cd /root/ddns_client
./install.sh

# 3. Configure AWS credentials
mkdir -p ~/.aws
nano ~/.aws/credentials
# Add your AWS credentials

# 4. Start service
systemctl start ddns_client
systemctl enable ddns_client

# 5. Monitor
journalctl -u ddns_client -f
```

## 📍 Files Location

- **Binary**: `/home/ubuntu/src/cpp/tbox/bazel-bin/src/tools/ddns_client`
- **Deployment Package**: `/home/ubuntu/src/cpp/tbox/deploy/ddns_client/`
- **Tarball**: `/home/ubuntu/src/cpp/tbox/deploy/ddns_client-deployment.tar.gz` (42 MB)

## 🎯 Target Server

- **IPv6**: 2408:8256:3109:da21::b4f
- **Domain**: home.xiedeacc.com
- **Status**: Pingable, SSH not available yet

## ⚠️ Current Issue

SSH is not accessible on the target server. Before deployment:

1. **Enable SSH on target server** (2408:8256:3109:da21::b4f):
   ```bash
   apt-get install -y openssh-server
   systemctl enable ssh
   systemctl start ssh
   ```

2. **Ensure IPv6 listening** in `/etc/ssh/sshd_config`:
   ```
   ListenAddress ::
   PermitRootLogin yes
   ```

3. **Allow through firewall**:
   ```bash
   ufw allow 22/tcp
   ip6tables -A INPUT -p tcp --dport 22 -j ACCEPT
   ```

## 📚 Documentation

See `/home/ubuntu/src/cpp/tbox/deploy/ddns_client/` for:
- `SUMMARY.md` - Complete project summary
- `README.md` - User guide
- `DEPLOYMENT_INSTRUCTIONS.md` - Detailed deployment guide

## 🧪 Test Locally First

```bash
export AWS_ACCESS_KEY_ID=your_key
export AWS_SECRET_ACCESS_KEY=your_secret
/home/ubuntu/src/cpp/tbox/bazel-bin/src/tools/ddns_client
```

Press Ctrl+C to stop.

## ✅ Next Steps

1. Enable SSH on target server (2408:8256:3109:da21::b4f)
2. Run deployment script or manual steps above
3. Configure AWS credentials
4. Monitor with `journalctl -u ddns_client -f`

---

**Need help?** See the full documentation in `/home/ubuntu/src/cpp/tbox/deploy/ddns_client/`
