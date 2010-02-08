EXTRA_DIST	= \
    dist db mutex dbinc dbinc_auto btree hash qam rep os repmgr lock common \
    dbm dbreg env fileops hmac hsearch log mp sequence clib txn db_archive \
    db_checkpoint db_deadlock db_dump db_hotbackup db_load db_printlog \
    db_recover db_sql db_stat db_upgrade db_verify test

all-local:
	cd $(builddir)/build_unix && make all

install-exec-local:
	cd $(builddir)/build_unix && make install_lib

uninstall-local:
	cd $(builddir)/build_unix && make uninstall_lib

clean-local:
	cd $(builddir)/build_unix && make clean

distclean-local:
	cd $(builddir)/build_unix && make distclean
