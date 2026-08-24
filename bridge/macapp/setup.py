"""py2app build script for the TokenSlate menu bar companion app.

    cd bridge/macapp && python3 setup.py py2app

Produces dist/TokenSlate.app. Requires the `tokenslate` package to be
installed (pip install -e bridge/) so its modules can be found.
"""

import os

from setuptools import setup

APP_VERSION = os.environ.get("TOKENSLATE_APP_VERSION", "0.1.0")

APP = ["tokenslate_app.py"]
OPTIONS = {
    "argv_emulation": False,
    "plist": {
        "CFBundleName": "TokenSlate",
        "CFBundleDisplayName": "TokenSlate",
        "CFBundleIdentifier": "com.tokenslate.macapp",
        "CFBundleShortVersionString": APP_VERSION,
        "LSUIElement": True,  # menu-bar only, no Dock icon
        "NSBluetoothAlwaysUsageDescription": (
            "TokenSlate needs Bluetooth to sync usage data to your "
            "T-Display-S3 device."
        ),
        "NSBluetoothPeripheralUsageDescription": (
            "TokenSlate needs Bluetooth to sync usage data to your "
            "T-Display-S3 device."
        ),
    },
}

setup(
    app=APP,
    options={"py2app": OPTIONS},
    setup_requires=["py2app"],
)
