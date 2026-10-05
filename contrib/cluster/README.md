# Cluster port helpers

`vm-sync-build.sh` rsyncs this checkout to the build VM (`VM`, default `wow@192.0.2.10.20`)
into `~/source/c9core-tc`, configures with the standard phase-1 flags, builds with
`make -j8`, and installs to `~/tc-335`. Output is written to `build/configure.log`,
`build/build.log`, `build/install.log` on the VM; the script prints only the error count
and the last line. Inspect failures with:

    ssh wow@192.0.2.10.20 "grep -n -B2 -A6 ' error:' ~/source/c9core-tc/build/build.log | head -80"
