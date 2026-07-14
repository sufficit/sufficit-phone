import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Control

import Linphone
import UtilsCpp
import SettingsCpp
import SufficitOAuthCpp
import 'qrc:/qt/qml/Linphone/view/Control/Tool/Helper/utils.js' as Utils
import 'qrc:/qt/qml/Linphone/view/Style/buttonStyle.js' as ButtonStyle

LoginLayout {
	id: mainItem
	property bool showBackButton: false
	signal goBack()
	signal goToRegister()

	titleContent: [
		BigButton {
			enabled: mainItem.showBackButton
			opacity: mainItem.showBackButton ? 1.0 : 0
            Layout.leftMargin: Utils.getSizeWithScreenRatio(79)
			icon.source: AppIcons.leftArrow
			style: ButtonStyle.noBackground
			onClicked: {
				console.debug("[LoginLayout] User: return")
				mainItem.goBack()
			}
			//: Return
			Accessible.name: qsTr("return_accessible_name")
		},
		RowLayout {
            spacing: Utils.getSizeWithScreenRatio(15)
            Layout.leftMargin: Utils.getSizeWithScreenRatio(21)
			EffectImage {
				fillMode: Image.PreserveAspectFit
				imageSource: AppIcons.profile
				colorizationColor: DefaultStyle.main2_600
                Layout.preferredHeight: Utils.getSizeWithScreenRatio(34)
                Layout.preferredWidth: Utils.getSizeWithScreenRatio(34)
			}
			Text {
                //: Connexion
                text: qsTr("assistant_account_login")
				font {
                    pixelSize: Typography.h1.pixelSize
                    weight: Typography.h1.weight
				}
			}
		},
		Item {
			Layout.fillWidth: true
		}
	]
	centerContent: [
		Flickable {
			anchors.left: parent.left
			anchors.top: parent.top
            anchors.leftMargin: Utils.getSizeWithScreenRatio(127)
			anchors.bottom: parent.bottom
			ColumnLayout {
				id: content
				spacing: 0
				BigButton {
                    Layout.preferredWidth: Utils.getSizeWithScreenRatio(361)
                    Layout.preferredHeight: Utils.getSizeWithScreenRatio(47)
					enabled: !SufficitOAuthCpp.loggingIn && !SufficitOAuthCpp.waitingForRamal
                    text: SufficitOAuthCpp.waitingForRamal
                        ? "Waiting for ramal assignment..."
                        : (SufficitOAuthCpp.loggingIn ? "Signing in..." : "Sign in with Sufficit")
					style: ButtonStyle.main
					onClicked: {SufficitOAuthCpp.login()}
				}
			}
		},
		Image {
			z: -1
			anchors.top: parent.top
			anchors.right: parent.right
            anchors.topMargin: Utils.getSizeWithScreenRatio(129)
            anchors.rightMargin: Utils.getSizeWithScreenRatio(127)
            width: Utils.getSizeWithScreenRatio(395)
            height: Utils.getSizeWithScreenRatio(350)
			fillMode: Image.PreserveAspectFit
			source: AppIcons.loginImage
		}
	]
}
 
