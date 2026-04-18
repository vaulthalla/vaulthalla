#!/bin/bash

rm -rf pkgroot
mkdir -p pkgroot/usr/share/vaulthalla-web
cp -a .next/standalone/. pkgroot/usr/share/vaulthalla-web/
mkdir -p pkgroot/usr/share/vaulthalla-web/.next
cp -a .next/static pkgroot/usr/share/vaulthalla-web/.next/
cp -a public pkgroot/usr/share/vaulthalla-web/ || true
