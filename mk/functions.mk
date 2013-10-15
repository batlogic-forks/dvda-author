#Do not use bash. It would cause subtle issues with libtool in the flac package, probably a libtool bug.

define trace
	echo "$1:  $2" >> BUILD.TRACE
endef

define follow
	$(if "$1", $(call trace,done,"$1 $2"), $(call trace,failed,$1))
endef

define execfollow
	$(if "$2",
	    program=$$(find /home/fab/Dev/dvda-author-dev/$1 -maxdepth 2 -type f -name $(strip $2)$(EXEEXT) -print0)
	    $(call follow,$$program,executable: `$$program $3` ),
	    $(call trace,failed,$2))
endef

define docfollow
	findstring=$$(find /home/fab/Dev/dvda-author-dev -maxdepth 1 -name $(strip $1) -print0)
	$(call follow, $1, $$findstring)
endef

#the complex autotools invocation is rendered necessary by the missing/obsolete status of the dvdauthor autotools chain
#notice autoconf twice and aclocal -I... to retrieve missing macros
#Theres is an odd MKDIR_P bug with MIGW32, which is circumvented here for generality but could be taken out in later versions

define configure_sub_package
	@target_subdir=$(strip $1)
	configure_flags="$2"
	echo configuring sub package with arguments: $1 $2 $3 $4
	if test "$$target_subdir" != ""; then
	   if test -d  "$$target_subdir"; then
	      cd "$$target_subdir"
	      if test "$3" != ""; then $(SHELL) "$3" $4; fi
	      $(SHELL) configure $$configure_flags --prefix="/home/fab/Dev/dvda-author-dev/local" CPPFLAGS="-I/home/fab/Dev/dvda-author-dev/local/include" \
		 && $(MAKE) MKDIR_P="mkdir$(EXEEXT) -p"
	      if test "$$?" = "0"; then
		 echo Installing from $$target_subdir ...
		 if test -f INSTALL; then mv -f INSTALL INSTALL.txt ; fi
		 $(MAKE) $(INS_BASE) install
	      fi
	      if test -f INSTALL.txt; then mv -f INSTALL.txt INSTALL; fi
	      cd -
	   fi
	fi
endef

define configure_lib_package
	@flags="$(CONFIGURE_$1_FLAGS)"
	echo $1: $$flags>>flags.test
	directory=$(MAYBE_$1)
	echo configuring lib_package with arguments: $1 $2 $3
	echo in directory $$directory
	echo with build flags $$flags
	$(call configure_sub_package,$$directory,$$flags,$2,$3)
	testvar=$$(find /home/fab/Dev/dvda-author-dev/$$directory -maxdepth 2 -type f -name $(strip $1).a -print0)
	$(if $$testvar,
			echo "found library: $$testvar for $1" >> BUILD.TRACE
			testvar=$$(find /home/fab/Dev/dvda-author-dev/local/lib -maxdepth 1 -name $(strip $1).a -print0)
			$(if $$testvar,
				echo "locally installed library: $$testvar from $1" >> BUILD.TRACE,
				echo "did not install library $1" >> BUILD.TRACE),
			echo "no library $1" >> BUILD.TRACE)
endef

define configure_exec_package
    flags=$(CONFIGURE_$1_FLAGS)
    directory=$(MAYBE_$1)
    echo configuring exec_package with arguments: $1 $2 $3
    ifeq $(build_os) mingw32
	patchfile=$$(find patches -type f -regex .*$1-patch.* -print0)
	echo Found mingw32 OS...patching with local patch $$patchfile
	$(if $$patchfile, patch -p0 < $$patchfile && echo "locally patched: $1, using $$patchfile in patches/" >> PATCHED.DOWNLOADS)
    endif
	$(call configure_sub_package,$$directory,$$flags)
	$(call execfollow,$$directory,$2,$3)
endef

define clean_package
	$(if $1,$(if $2, (if test -d  $2; then cd $2; $(MAKE)  clean ; cd - ; fi)))
endef

define depconf
	if test "$($1_MAKESPEC)" = "auto" ; then
	  if test "$($1_CONFIGSPEC)" = "lib"; then
	     if test $(origin $($1_COMMANDLINE)) != undefined ; then
		$(call configure_lib_package,$1,$1,$($1_COMMANDLINE))
	     else
		$(call configure_lib_package,$1)
	     fi
	  fi
	fi
endef

define clean_directory
	for dir in $1; do
	   if test -d "$$dir" ; then
	     cd "$$dir"; $(RM) *.a *.po *.o; cd /home/fab/Dev/dvda-author-dev
	   fi
	done
endef
