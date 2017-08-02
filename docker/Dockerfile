FROM centos:6

RUN yum -y install rpm-build redhat-rpm-config make gcc tar yum-utils \
    autoconf libtool git zlib-devel libxml2-devel doxygen byacc flex tcl \
    libpng-devel \
    && yum clean all

WORKDIR /root
RUN mkdir -p /usr/src/redhat /source /target \
    /root/rpmbuild/{SOURCES,RPMS,SRPMS,BUILDROOT}

CMD ["/bin/bash"]