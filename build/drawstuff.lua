package.name = "drawstuff"
package.language = "c++"
package.objdir = "obj/drawstuff"


-- Append a "d" to the debug version of the libraries

  for k,v in ipairs(project.configs) do
    if (string.find(v, "Debug") ~= nil) then
      package.config[v].target = "drawstuffd"
    end
  end
  
  
-- Separate distribution files into toolset subdirectories

  if (options["usetargetpath"]) then
    package.path = options["target"]
  else
    package.path = "custom"
  end


-- Package Build Settings

  local dll_defines =
  {
    "DS_DLL",
    "USRDLL"
  }
  
  local lib_defines = 
  {
    "DS_LIB"
  }
      
  if (options["enable-shared-only"]) then
    package.kind = "dll"
    table.insert(package.defines, dll_defines)
  elseif (options["enable-static-only"]) then
    package.kind = "lib"
    table.insert(package.defines, lib_defines)
  else
    package.config["DebugSingleDLL"].kind = "dll"
    package.config["DebugSingleLib"].kind = "lib"
    package.config["ReleaseSingleDLL"].kind = "dll"
    package.config["ReleaseSingleLib"].kind = "lib"

    table.insert(package.config["DebugSingleDLL"].defines, dll_defines)
    table.insert(package.config["ReleaseSingleDLL"].defines, dll_defines)
    table.insert(package.config["DebugSingleLib"].defines, lib_defines)
    table.insert(package.config["ReleaseSingleLib"].defines, lib_defines)

    package.config["DebugDoubleDLL"].kind = "dll"
    package.config["DebugDoubleLib"].kind = "lib"
    package.config["ReleaseDoubleDLL"].kind = "dll"
    package.config["ReleaseDoubleLib"].kind = "lib"

    table.insert(package.config["DebugDoubleDLL"].defines, dll_defines)
    table.insert(package.config["ReleaseDoubleDLL"].defines, dll_defines)
    table.insert(package.config["DebugDoubleLib"].defines, lib_defines)
    table.insert(package.config["ReleaseDoubleLib"].defines, lib_defines)

    table.insert(package.config["DebugDoubleDLL"].defines, "dDOUBLE")
    table.insert(package.config["ReleaseDoubleDLL"].defines, "dDOUBLE")
    table.insert(package.config["DebugDoubleLib"].defines, "dDOUBLE")
    table.insert(package.config["ReleaseDoubleLib"].defines, "dDOUBLE")
  end

  package.includepaths =
  {
    "../../include",
    "../../ode/src"
  }

  -- disable VS2005 CRT security warnings
  if (options["target"] == "vs2005") then
    table.insert(package.defines, "_CRT_SECURE_NO_DEPRECATE")
  end


-- Build Flags

	package.config["DebugSingleLib"].buildflags   = { }
	package.config["DebugSingleDLL"].buildflags   = { }

	package.config["ReleaseSingleDLL"].buildflags = { "optimize-speed", "no-symbols", "no-frame-pointer" }
	package.config["ReleaseSingleLib"].buildflags = { "optimize-speed", "no-symbols", "no-frame-pointer" }

	package.config["DebugDoubleLib"].buildflags   = { }
	package.config["DebugDoubleDLL"].buildflags   = { }

	package.config["ReleaseDoubleDLL"].buildflags = { "optimize-speed", "no-symbols", "no-frame-pointer" }
	package.config["ReleaseDoubleLib"].buildflags = { "optimize-speed", "no-symbols", "no-frame-pointer" }

	if (options.target == "vs6" or options.target == "vs2002" or options.target == "vs2003") then
      for k,v in ipairs(project.configs) do
        if (string.find(v, "Lib") ~= nil) then
          table.insert(package.config[v].buildflags, "static-runtime")
        end
      end
	end


-- Libraries

  local windows_libs =
  {
    "user32",
    "opengl32",
    "glu32",
    "winmm",
    "gdi32"
  }

  local x11_libs =
  {
    "X11",
    "GL",
    "GLU"
  }
  
  if (windows) then
    table.insert(package.links, windows_libs)
  else
    table.insert(package.links, x11_libs)
  end


-- Files

  package.files =
  {
    matchfiles("../../include/drawstuff/*.h"),
    "../../drawstuff/src/internal.h",
    "../../drawstuff/src/drawstuff.cpp"
  }

  if (windows) then
    table.insert(package.defines, "WIN32")
    table.insert(package.files, "../../drawstuff/src/resource.h")
    table.insert(package.files, "../../drawstuff/src/resources.rc")
    table.insert(package.files, "../../drawstuff/src/windows.cpp")
  else
    table.insert(package.files, "../../drawstuff/src/x11.cpp")
  end
