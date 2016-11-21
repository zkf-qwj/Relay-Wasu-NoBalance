echo "buildit clean"

echo "rm .o(s)"
rm -rf ./*.o
rm -rf ./*/*.o
rm -rf ./*/*/*.o
rm -rf ./*/*/*/*.o
rm -rf ./*/*/*/*/*.o
rm -rf ./QTFileLib/*.xo
rm -rf ./SafeStdLib/*.xo
echo "rm .a(s)"
rm -rf ./*.a
rm -rf ./*/*.a
rm -rf ./*/*/*.a
rm -rf ./*/*/*/*.a
rm -rf ./*/*/*/*/*.a
echo "rm .DS_Store(s) "
rm -rf ./.DS_Store
rm -rf ./*/.DS_Store
rm -rf ./*/*/.DS_Store
rm -rf ./*/*/*/.DS_Store
rm -rf ./*/*/*/*/.DS_Store
rm -rf ./*/*/*/*/*/.DS_Store
rm -rf ./*/*/*/*/*/*/.DS_Store
echo "rm .opt(s) "
rm -rf ./*.opt
rm -rf ./*/*.opt
rm -rf ./*/*/*.opt
rm -rf ./*/*/*/*.opt
rm -rf ./*/*/*/*/*.opt
echo "rm .plg(s) "
rm -rf ./*.plg
rm -rf ./*/*.plg
rm -rf ./*/*/*.plg
rm -rf ./*/*/*/*.plg
rm -rf ./*/*/*/*/*.plg
echo "rm .ilk(s) "
rm -rf ./*.ilk
rm -rf ./*/*.ilk
rm -rf ./*/*/*.ilk
rm -rf ./*/*/*/*.ilk
rm -rf ./*/*/*/*/*.ilk
echo "rm .exp(s) "
rm -rf ./*.exp
rm -rf ./*/*.exp
rm -rf ./*/*/*.exp
rm -rf ./*/*/*/*.exp
rm -rf ./*/*/*/*/*.exp
echo "rm .ncb(s) "
rm -rf ./*.ncb
rm -rf ./*/*.ncb
rm -rf ./*/*/*.ncb
rm -rf ./*/*/*/*.ncb
rm -rf ./*/*/*/*/*.ncb
echo "rm .sbr(s) "
rm -rf ./*.sbr
rm -rf ./*/*.sbr
rm -rf ./*/*/*.sbr
rm -rf ./*/*/*/*.sbr
rm -rf ./*/*/*/*/*.sbr
echo "rm .map(s) "
rm -rf ./*.map
rm -rf ./*/*.map
rm -rf ./*/*/*.map
rm -rf ./*/*/*/*.map
rm -rf ./*/*/*/*/*.map
echo "rm .bsc(s) "
rm -rf ./*.bsc
rm -rf ./*/*.bsc
rm -rf ./*/*/*.bsc
rm -rf ./*/*/*/*.bsc
rm -rf ./*/*/*/*/*.bsc
echo "rm .pdb(s) "
rm -rf ./*.pdb
rm -rf ./*/*.pdb
rm -rf ./*/*/*.pdb
rm -rf ./*/*/*/*.pdb
rm -rf ./*/*/*/*/*.pdb
echo "rm .obj(s)"
rm -rf ./*.obj
rm -rf ./*/*.obj
rm -rf ./*/*/*.obj
rm -rf ./*/*/*/*.obj
rm -rf ./*/*/*/*/*.obj
echo "rm objects-* "
rm -rf ./objects-*
rm -rf ./*/objects-*
rm -rf ./*/*/objects-*
rm -rf ./*/*/*/objects-*
rm -rf ./*/*/*/*/objects-*
echo "rm obj-* "
rm -rf ./obj-*
rm -rf ./*/obj-*
rm -rf ./*/*/obj-*
rm -rf ./*/*/*/obj-*
rm -rf ./*/*/*/*/obj-*
echo "rm *.dylib "
rm -rf ./*.dylib 
echo "rm *.dylib "
rm -rf ./*.A.dylib 
echo "rm *.exe "
rm -rf ./*.exe
rm -rf ./*/*.exe
rm -rf ./*/*/*.exe
rm -rf ./*/*/*/*.exe
rm -rf ./*/*/*/*/*.exe
echo "rm cvs diffs"
rm -rf ./.#*
rm -rf ./*/.#*
rm -rf ./*/*/.#*
rm -rf ./*/*/*/.#*
rm -rf ./*/*/*/*/.#*
echo "rm *.tar "
rm -rf ./*.tar
rm -rf ./*/*.tar
rm -rf ./*/*/*.tar
rm -rf ./*/*/*/*.tar
rm -rf ./*/*/*/*/*.tar
echo "rm *.gz "
rm -rf ./*.gz
rm -rf ./*/*.gz
rm -rf ./*/*/*.gz
rm -rf ./*/*/*/*.gz
rm -rf ./*/*/*/*/*.gz

echo "rm DarwinStreamingServer"
rm -f ./DarwinStreamingServer
echo "rm QuickTimeStreamingServer"
rm -f ./QuickTimeStreamingServer
echo "rm PlaylistBroadcaster from ./"
rm -f ./PlaylistBroadcaster
echo "rm QT Tools from ./"
rm -f ./QTSDPGen
rm -f ./QTBroadcaster
rm -f ./QTFileInfo
rm -f ./QTFileTest
rm -f ./QTSampleLister
rm -f ./QTTrackInfo
rm -f ./QTRTPFileTest
rm -f ./QTRTPGen


echo "rm ..build"
rm -rf ./..build
rm -rf ./build
rm -rf ./Build

echo "rm Installers "
rm -rf ./QuickTImeStreamingServer.pkg
rm -rf ./DarwinStreamingServer.pkg

echo "rm Executables "
rm -f PlaylistBroadcaster
rm -f QTFileInfo
rm -f QTFileTest
rm -f QTRTPGen
rm -f QTSDPGen
rm -f QTFileInfo
rm -f QTTrackInfo
rm -f QuickTimeStreamingServer
rm -f ./*/StreamingLoadTool
rm -rf ./StreamingProxy-5.0-MacOSX

rm -f ./PlaylistBroadcaster
rm -f ./*/PlaylistBroadcaster

rm -f ./QuickTimeStreamingServer
rm -f ./*/QuickTimeStreamingServer

rm -f ./StreamingProxy
rm -f ./*/StreamingProxy

rm -f ./QTBroadcaster
rm -f ./*/QTBroadcaster
rm -f ./*/*/QTBroadcaster

rm -f ./*/MP3Broadcaster

rm -f ./QTFileInfo
rm -f ./*/QTFileInfo
rm -f ./*/*/QTFileInfo

rm -f ./QTFileTest
rm -f ./*/QTFileTest
rm -f ./*/*/QTFileTest

rm -f ./QTRTPFileTest
rm -f ./*/QTRTPFileTest
rm -f ./*/*/QTRTPFileTest

rm -f ./QTRTPGen
rm -f ./*/QTRTPGen
rm -f ./*/*/QTRTPGen

rm -f ./QTSDPGen
rm -f ./*/QTSDPGen
rm -f ./*/*/QTSDPGen

rm -f ./QTSampleLister
rm -f ./*/QTSampleLister
rm -f ./*/*/QTSampleLister

rm -f ./QTSampleLister
rm -f ./*/QTSampleLister
rm -f ./*/*/QTSampleLister

rm -f ./QTTrackInfo
rm -f ./*/QTTrackInfo
rm -f ./*/*/QTTrackInfo

rm -f ./RTPFileGen
rm -f ./*/RTPFileGen
rm -f ./*/*/RTPFileGen

rm -rf ./*.bundle


echo "rm miscellaneous "
rm -f ./qtpasswd.tproj/qtpasswd
rm -f ./qtdigest.tproj/qtdigest
rm -f ./WinNTSupport/DarwinStreamingServer.lib
rm -f ./WinNTSupport/*.opt
rm -f ./APIModules/QTSSSpamDefenseModule.bproj/QTSSSpamDefenseModule
rm -f ./APIModules/QTSSRawFileModule.bproj/QTSSRawFileModule
rm -f ./APIModules/QTSSDemoAuthorizationModule.bproj/QTSSDemoAuthorizationModule
rm -f ./APIModules/QTSSAccessModule.bproj/QTSSAccessModule
rm -f ./result
rm -rf ./StreamingServer.pbproject/StreamingServer.pbproj
echo "rm Win modules"
rm -rf ./WinNTSupport/dynmodules/
rm -rf ./WinNTSupport/dynmodules_disabled/

echo "rm Win Build directories"
./deleteWinBuildDirs .
rm -rf ./WinNTSupport/build/
rm -rf ./WinNTSupport/DarwinStreamingServer/
rm -f ./WinNTSupport/StreamingServer.sln
rm -f ./WinNTSupport/StreamingServer.suo

rm ./.gdb_history

