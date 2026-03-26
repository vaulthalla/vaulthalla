for dir in /tmp/vh_mount /tmp/vh_backing ; do
  sudo mkdir $dir
  sudo chmod -R 755 $dir
  sudo chown -R vaulthalla:vaulthalla $dir
done
