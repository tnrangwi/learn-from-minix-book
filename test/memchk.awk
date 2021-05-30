#Read a tab separated memdebug output as generated with a MEM_DBG compile
#Create output by redirecting stderr to a file.
#Run with -F set to tab, e.g. awk -F '	' -f memchk.awk memfile
($1 == "A") {
    if ($2 in alloc) {
        print "Internal error: double allocate:", $2
        exit 1
    }
    alloc[$2] = $3":"$4
}
($1 == "F") {
    if ($2 in alloc) {
        delete alloc[$2]
    } else {
        print "Internal error: de-allocate invalid:$0"
        exit 1
    }
}
END {
    error = 0;
    for (v in alloc) {
        if (error == 0) {
            print "Memory not freed:";
            error = 1
        }
        print v, alloc[v]
    }
}
