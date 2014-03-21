group { "puppet":
  ensure => "present",
}

File { owner => 0, group => 0, mode => 0644 }
Exec { path => ['/usr/bin', '/bin', '/usr/sbin', '/sbin'], }

exec {'update': command => 'apt-get --assume-yes update', }

package {'make':
  ensure  => present,
}

package {'libxml2-dev':
  ensure  => present,
}

package {'libpng-dev':
  ensure  => present,
}

exec {'bashrc':
  command => 'echo "set -o vi" >>/home/vagrant/.bashrc',
  unless  => 'grep "set *-o" /home/vagrant/.bashrc',
}