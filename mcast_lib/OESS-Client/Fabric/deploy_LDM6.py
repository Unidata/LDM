#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""@package deploy_LDM6
Copyright (C) 2015 University of Virginia. All rights reserved.

file      deploy_LDM6.py
author    Shawn Chen <sc7cq@virginia.edu>
version   1.0
date      Nov. 8, 2015

LICENSE
This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2 of the License, or（at your option）
any later version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details at http://www.gnu.org/copyleft/gpl.html

brief     Installs and deploys LDM6 on the testbed.
"""

from __future__ import division
import logging
import sys
import math
from StringIO import StringIO
from fabric.api import env, run, cd, get, sudo, put
from fabric.context_managers import settings

logging.basicConfig()
paramiko_logger = logging.getLogger("paramiko.transport")
paramiko_logger.disabled = True

LDM_VER = 'ldm-6.12.15.42'
LDM_PACK_NAME = LDM_VER + '.tar.gz'
LDM_PACK_PATH = '~/Workspace/'
TC_RATE = 20 # Mbps
RTT = 89 # ms
SINGLE_BDP = TC_RATE * 1000 * RTT / 8 # bytes
RCV_NUM = 1 # number of receivers
LOSS_RATE = 0.01

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
    put('~/Workspace/CTCP.zip', '/root', mode=0664)
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
        with cd('/home/ldm/%s/src' % LDM_VER):
            sudo('make distclean', quiet=True)
            sudo('./configure --with-debug --prefix=/home/ldm \
                 --disable-root-actions CFLAGS=-g CXXFLAGS=-g')
            sudo('make install')
            run('make root-actions')

def install_config_ctcp():
    """
    Compiles, installs and configures CTCP on the system.
    """
    run('yum -y install kernel-devel-$(uname -r)')
    run('unzip -o CTCP.zip')
    with cd('/root/CTCP'):
        run('make')
        run('insmod ./tcp_ctcp.ko', quiet=True)
    run('sysctl -w net.ipv4.tcp_congestion_control="ctcp"')
    run('echo %s > /sys/module/tcp_ctcp/parameters/bw' % str(TC_RATE))
    run('echo %s > /sys/module/tcp_ctcp/parameters/initial' %
        str(int(math.ceil(float(SINGLE_BDP/1500)))))
    run('echo %s > /sys/module/tcp_ctcp/parameters/scale' % str(120))

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
        config_str = ('ALLOW ANY ^.*$\nEXEC \"insert.sh\"'
                      '\nEXEC \"cpu_mon.sh\"\nEXEC \"tc_mon.sh\"')
        run('tc qdisc del dev eth1 root', quiet=True)
        run('tc qdisc add dev eth1 root tbf rate %smbit burst 50kb limit \
            %sb' % (str(TC_RATE*RCV_NUM), str(2*SINGLE_BDP*RCV_NUM)), quiet=True)
        with cd('/home/ldm'):
            sudo('git clone \
                 https://github.com/shawnsschen/LDM6-LDM7-comparison.git',
                 user='ldm', quiet=True)
        install_config_ctcp()
        sudo('regutil -s 5G /queue/size', user='ldm')
    else:
        config_str = 'REQUEST ANY .* 10.10.1.1'
        sudo('regutil -s 3G /queue/size', user='ldm')
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
        sudo('run_ldm ldmd_test')

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
            sudo('sar -n DEV | grep eth1 > bandwidth.log')
            get('cpu_measure.log', '~/Workspace/LDM6-LDM7-LOG/')
            get('bandwidth.log', '~/Workspace/LDM6-LDM7-LOG/')
            get('tc_mon.log', '~/Workspace/LDM6-LDM7-LOG/')

def patch_sysctl():
    """
    Patches the core mem size in sysctl config.
    """
    run('sysctl -w net.core.rmem_max=%s' % str(1*1024*1024*1024))
    run('sysctl -w net.core.wmem_max=%s' % str(1*1024*1024*1024))
    run('sysctl -w net.core.rmem_default=%s' % str(1*1024*1024*1024))
    run('sysctl -w net.core.wmem_default=%s' % str(1*1024*1024*1024))

def add_loss():
    """
    Adds loss in iptables.
    """
    run('iptables -A INPUT -i eth1 -m statistic --mode random \
        --probability %s -j DROP' % str(LOSS_RATE))

def rm_loss():
    """
    Removes loss in iptables.
    """
    run('iptables -D INPUT -i eth1 -m statistic --mode random \
        --probability %s -j DROP' % str(LOSS_RATE))

def deploy():
    clear_home()
    upload_pack()
    install_pack()
    init_config()
