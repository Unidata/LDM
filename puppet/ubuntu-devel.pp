group { "puppet":
  ensure => "present",
}

File { owner => 0, group => 0, mode => 0644 }
Exec { path => ['/usr/bin', '/bin', '/usr/sbin', '/sbin'], }

exec {'update': command => 'apt-get -y update', }

package {'make':
  ensure  => present,
  require => Exec['update'],
}

package {'libxml2-dev':
  ensure  => present,
  require => Exec['update'],
}

package {'libpng-dev':
  ensure  => present,
  require => Exec['update'],
}

package {'libyaml-dev':
  ensure  => present,
  require => Exec['update'],
}

exec {'bashrc':
  command => 'echo "set -o vi" >>/home/vagrant/.bashrc',
  unless  => 'grep "set *-o" /home/vagrant/.bashrc',
}