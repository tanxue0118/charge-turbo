echo
echo 查询手机温控
echo
echo 作者：御坂Thepoor  版本：v1.3
echo 联系QQ群：1048572965  
echo
echo 如果运行不成功，请检查你是否加了ROOT权限再执行的
echo
echo Ready....... 
sleep 2s
find /system /vendor /product /system_ext -type f -iname "*thermal*" -exec ls -s -h {} \; 2>/dev/null | sed '/hardware/d'
echo
echo 请检查文件名前的数字是否为0，如果为0则屏蔽成功，否则失败
echo ------------------------------------------------
echo 如果没有屏蔽成功，请自己去vendor/etc找到thermal开头的文件
echo ------------------------------------------------
echo so文件是驱动，可能导致卡米，不要管.....
echo ------------------------------------------------
echo 如果检测到全是1kb或4kb的那就是脚本抽风，但实际上已经屏蔽了-------------------------------------
echo 手动退出或者等自动退出
sleep 15