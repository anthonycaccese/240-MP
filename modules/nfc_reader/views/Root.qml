import QtQuick

FocusScope {
    id: moduleRoot

    signal goBack()

    property var navParams: ({})

    property string moduleId: "com.240mp.nfc_reader"
    property var _moduleInfo: appCore ? appCore.get_module_info(moduleId) : ({})
    property string moduleName: _moduleInfo.name || ""
    property string moduleIcon: _moduleInfo.icon || ""

    property var navStack: []
    property var currentParams: ({})

    function navigateTo(viewPath, params, fromState) {
        var resolved = Qt.resolvedUrl(viewPath)
        navStack.push({ source: internalLoader.source, params: currentParams, listState: fromState || {} })
        currentParams = params || {}
        internalLoader.setSource(resolved, { "navParams": params || {} })
    }

    function navigateBack() {
        if (navStack.length === 0) {
            moduleRoot.goBack()
            return
        }
        var prev = navStack.pop()
        if (!prev.source || prev.source.toString() === "") {
            moduleRoot.goBack()
            return
        }
        var restored = Object.assign({}, prev.params)
        restored.navListState = prev.listState || {}
        currentParams = restored
        internalLoader.setSource(prev.source, { "navParams": restored })
    }

    Loader {
        id: internalLoader
        anchors.fill: parent
        focus: true
        onLoaded: { if (item) item.forceActiveFocus() }

        Connections {
            target: internalLoader.item
            ignoreUnknownSignals: true
            function onNavigateTo(path, params, listState) { moduleRoot.navigateTo(path, params, listState) }
            function onGoBack() { moduleRoot.navigateBack() }
        }
    }

    // With PC/SC missing no child view ever loads, so the back keys for the
    // unavailable screen must be handled here.
    Keys.onPressed: function(event) {
        if (!nfcReaderBackend.available &&
            (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back)) {
            moduleRoot.goBack()
            event.accepted = true
        }
    }

    // Shown when PCSC library is not available at build time
    Column {
        visible: !nfcReaderBackend.available
        anchors.centerIn: parent
        spacing: root.sh * 0.02

        Text {
            text: "NFC Reader"
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.05
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            text: "PCSC library not found"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.033
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            text: "On Raspberry Pi, install libpcsclite-dev\nand rebuild. See the wiki for details."
            color: root.tertiaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.025
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            lineHeight: 1.4
        }
    }

    Text {
        visible: !nfcReaderBackend.available
        text: root.hints.back + ":BACK"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }

    Component.onCompleted: {
        if (nfcReaderBackend.available) {
            nfcReaderBackend.setModuleActive(true)
            nfcReaderBackend.reloadMapping()
            navigateTo("Items.qml", {})
        }
    }

    // Card taps must do nothing once the user leaves the module.
    Component.onDestruction: nfcReaderBackend.setModuleActive(false)
}
