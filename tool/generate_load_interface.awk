BEGIN {
  # Set field seperator
  FS="[{} \"]*"
}

FNR == 1 {
  # Clear `namespace` for each input file
  namespace=""
}

/\$Cxx.namespace\("[^"]+"\);/ {
  # Get C++ namespace name
  namespace=$2
}

/^interface / {
  # Get interface name
  interface=$2

  # Skip template interface
  #   (template interfaces should be depended from other interfaces)
  if (match(interface, "\\(.*\\)")) {
    next
  }

  # Append namespace if exists
  if (namespace!="") {
    interface=namespace "::" interface
  }

  # Print C++ code to load interface
  print "  loader.loadCompiledTypeAndDependencies<" interface ">();"
}
