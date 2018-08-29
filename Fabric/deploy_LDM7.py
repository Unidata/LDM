#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""@package deploy_LDM7
Copyright (C) 2015 University of Virginia. All rights reserved.

file      deploy_LDM7.py
author    Shawn Chen <sc7cq@virginia.edu>
version   1.0
date      Oct. 28, 2015

LICENSE
This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2 of the License, or（at your option）
any later version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details at http://www.gnu.org/copyleft/gpl.html

brief     Installs and deploys LDM7 on the GENI testbed.
"""

import logging
import sys
from StringIO import StringIO
from fabric.api import env, run, cd, get, sudo, put
from fabric.context_managers import settings

logging.basicConfig()
paramiko_logger = logging.getLogger("paramiko.transport")
paramiko_logger.disabled = True

LDM_VER = 'ldm-6.13.2.6'
LDM_PACK_NAME = LDM_VER + '.tar.gz'
LDM_PACK_PATH = '~/Workspace/'
TC_RATE = 20 # Mbps
RTT = 1 # ms
SINGLE_BDP = TC_RATE * 1000 * RTT / 8 # bytes
RCV_NUM = 2 # number of receivers
LOSS_RATE = 0.01
IFACE_NAME = 'eth1'

def read_hosts():
    """
    Reads hosts IP from sys.stdin line by line, expecting one per line.
    Then appends the username to each IP address.
    """
    env.hosts = []
    for line in sys.stdin.readlines():
        host = line.strip()
        if host and not host.startswith("#"):
            host = 'root@' + host
            env.hosts.append(host)

def clear_home():
    """
    Clears the ldm user home directory, including the existing product queue.
    """
    with cd('/home/ldm'):
        run('rm -rf *')

def upload_pack():
    """
    Uploads the LDM source code package onto the test node. Also uploads a
    LDM start script.
    """
    put(LDM_PACK_PATH + LDM_PACK_NAME, '/home/ldm', mode=0664)
    put('~/Workspace/CC-NIE-Toolbox/generic/misc/util/', '/home/ldm',
        mode=0664)
    with cd('/home/ldm'):
        run('chown ldm.ldm %s' % LDM_PACK_NAME)
        run('chmod +x util/run_ldm util/insert.sh util/cpu_mon.sh util/tc_mon.sh')
        run('chown -R ldm.ldm util')

def install_pack():
    """
    Compiles and installs the LDM source code.
    """
    with settings(sudo_user='ldm'):
        with cd('/home/ldm'):
            sudo('gunzip -c %s | pax -r \'-s:/:/src/:\'' % LDM_PACK_NAME)
        patch_linkspeed()
        #patch_frcv()
        with cd('/home/ldm/%s/src' % LDM_VER):
            sudo('make distclean', quiet=True)
            sudo('find -exec touch \{\} \;', quiet=True)
            sudo('./configure --with-debug --with-multicast \
                 --disable-root-actions CFLAGS=-g CXXFLAGS=-g')
            sudo('make CXXFLAGS="-DDEBUG1" install')
            run('make root-actions')

def init_config():
    """
    Configures the etc file and environment variables. Also sets up tc and
    routing table on the sender.
    """
    run('service ntpd start', quiet=True)
    run('service iptables start', quiet=True)
    run('yum -y install sysstat', quiet=True)
    run('sed -i -e \'s/*\/10/*\/1/g\' /etc/cron.d/sysstat', quiet=True)
    run('rm /var/log/sa/*', quiet=True)
    run('service crond start', quiet=True)
    run('service sysstat start', quiet=True)
    iface = run('hostname -I | awk \'{print $2}\'')
    if iface == '10.10.1.1':
        config_str = ('MULTICAST ANY 224.0.0.1:38800 1 10.10.1.1\n'
                      'ALLOW ANY ^.*$\nEXEC \"insert.sh\"'
                      '\nEXEC \"cpu_mon.sh\"\nEXEC \"tc_mon.sh\"')
        run('route add 224.0.0.1 dev %s' % IFACE_NAME, quiet=True)
        run('tc qdisc del dev %s root' % IFACE_NAME, quiet=True)
        run('tc qdisc add dev %s root handle 1: htb default 2' % IFACE_NAME, quiet=True)
        run('tc class add dev %s parent 1: classid 1:1 htb rate %smbit \
            ceil %smbit' % (IFACE_NAME, str(TC_RATE), str(TC_RATE)), quiet=True)
        run('tc qdisc add dev %s parent 1:1 handle 10: bfifo limit %sb' %
            (IFACE_NAME, '600m'), quiet=True)
        run('tc class add dev %s parent 1: classid 1:2 htb rate %smbit \
            ceil %smbit' % (IFACE_NAME, str(TC_RATE), str(TC_RATE)), quiet=True)
        run('tc qdisc add dev %s parent 1:2 handle 11: bfifo limit %sb' %
            (IFACE_NAME, '600m'), quiet=True)
        run('tc filter add dev %s protocol ip parent 1:0 prio 1 u32 match \
            ip dst 224.0.0.1/32 flowid 1:1' % IFACE_NAME, quiet=True)
        run('tc filter add dev %s protocol ip parent 1:0 prio 1 u32 match \
            ip dst 0/0 flowid 1:2' % IFACE_NAME, quiet=True)
        with cd('/home/ldm'):
            sudo('git clone \
                 https://github.com/shawnsschen/LDM6-LDM7-comparison.git',
                 user='ldm', quiet=True)
        sudo('regutil -s 5G /queue/size', user='ldm')
    else:
        config_str = 'RECEIVE ANY 10.10.1.1 ' + iface
        sudo('regutil -s 2G /queue/size', user='ldm')
        patch_sysctl()
    fd = StringIO()
    get('/home/ldm/.bashrc', fd)
    content = fd.getvalue()
    if 'ulimit -c "unlimited"' in content:
        update_bashrc = True
    else:
        update_bashrc = False
    get('/home/ldm/.bash_profile', fd)
    content = fd.getvalue()
    if 'export PATH=$PATH:$HOME/util' in content:
        update_profile = True
    else:
        update_profile = False
    with settings(sudo_user='ldm'):
        with cd('/home/ldm'):
            sudo('echo \'%s\' > etc/ldmd.conf' % config_str)
            if not update_bashrc:
                sudo('echo \'ulimit -c "unlimited"\' >> .bashrc')
            if not update_profile:
                sudo('echo \'export PATH=$PATH:$HOME/util\' >> .bash_profile')
        sudo('regutil -s %s /hostname' % iface)
        #sudo('regutil -s 5G /queue/size')
        sudo('regutil -s 35000 /queue/slots')

def start_LDM():
    """
    Start LDM and writes log file to a specified location.
    """
    with settings(sudo_user='ldm'), cd('/home/ldm'):
        sudo('ldmadmin mkqueue -f')
        sudo('ldmadmin start -v')

def stop_LDM():
    """
    Stops running LDM.
    """
    with settings(sudo_user='ldm'), cd('/home/ldm'):
        sudo('ldmadmin stop')

def fetch_log():
    """
    Fetches the LDM log.
    """
    iface = run('hostname -I | awk \'{print $2}\'')
    with cd('/home/ldm/var/logs'):
        run('mv ldmd_test.log %s.log' % iface)
    get('/home/ldm/var/logs/%s.log' % iface, '~/Workspace/LDM6-LDM7-LOG/')
    if iface == '10.10.1.1':
        with settings(sudo_user='ldm'), cd('/home/ldm'):
            sudo('sar -n DEV | grep %s > bandwidth.log' % IFACE_NAME)
            get('cpu_measure.log', '~/Workspace/LDM6-LDM7-LOG/')
            get('bandwidth.log', '~/Workspace/LDM6-LDM7-LOG/')
            get('tc_mon.log', '~/Workspace/LDM6-LDM7-LOG/')

def patch_linkspeed():
    """
    Patches the receiving side linkspeed.
    """
    with settings(sudo_user='ldm'), cd(
        '/home/ldm/%s/src/mcast_lib/vcmtp/VCMTPv3/receiver' % LDM_VER):
        sudo('sed -i -e \'s/linkspeed(20000000)/linkspeed(%s)/g\' \
             vcmtpRecvv3.cpp' % str(TC_RATE*1000*1000), quiet=True)

def patch_frcv():
    """
    Patches the frcv value.
    """
    with settings(sudo_user='ldm'), cd(
        '/home/ldm/%s/src/mcast_lib/vcmtp/VCMTPv3/receiver' % LDM_VER):
        sudo('sed -i -e \'s/Frcv 20/Frcv 5/g\' vcmtpRecvv3.cpp', quiet=True)

def patch_sysctl():
    """
    Patches the core mem size in sysctl config.
    """
    run('sysctl -w net.core.rmem_max=%s' % str(int(2*1000*1000*1000)))
    #run('sysctl -w net.core.wmem_max=%s' % str(1*1024*1024*1024))
    run('sysctl -w net.core.rmem_default=%s' % str(int(2*1000*1000*1000)))
    #run('sysctl -w net.core.wmem_default=%s' % str(1*1024*1024*1024))

def add_loss():
    """
    Adds loss in iptables.
    """
    run('iptables -A INPUT -i %s -m statistic --mode random \
        --probability %s -p udp -j DROP' % (IFACE_NAME, str(LOSS_RATE)))

def rm_loss():
    """
    Removes loss in iptables.
    """
    run('iptables -D INPUT -i %s -m statistic --mode random \
        --probability %s -p udp -j DROP' % (IFACE_NAME, str(LOSS_RATE)))

def deploy():
    clear_home()
    upload_pack()
    install_pack()
    init_config()
