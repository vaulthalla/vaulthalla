if id vaulthalla &>/dev/null; then
    echo "👤 Removing system user 'vaulthalla'..."
    sudo userdel -r vaulthalla || echo "⚠️  Could not delete home or user not removable"
else
    echo "✅ System user 'vaulthalla' not present."
fi

# Remove 'vaulthalla' group if it exists
if getent group vaulthalla > /dev/null; then
    echo "👥 Removing 'vaulthalla' group..."
    sudo groupdel vaulthalla || echo "⚠️  Could not delete group 'vaulthalla' or group not removable"
else
    echo "✅ 'vaulthalla' group not present."
fi
