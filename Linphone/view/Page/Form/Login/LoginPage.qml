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
	readonly property string ramalAssignmentUrl: "https://blazor.sufficit.com.br/pages/provisioning/my-devices"
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
				spacing: Utils.getSizeWithScreenRatio(12)
				BigButton {
                    Layout.preferredWidth: Utils.getSizeWithScreenRatio(361)
                    Layout.preferredHeight: Utils.getSizeWithScreenRatio(47)
					visible: !SufficitOAuthCpp.waitingForRamal
					enabled: !SufficitOAuthCpp.loggingIn && !SufficitOAuthCpp.resuming
                    //: Button shown while the Sufficit authentication is in progress.
                    //: Button shown while the saved Sufficit session is being restored.
                    text: SufficitOAuthCpp.loggingIn ? qsTr("Signing in...")
                        : (SufficitOAuthCpp.resuming ? qsTr("Restoring session...") : qsTr("Sign in with Sufficit"))
					style: ButtonStyle.main
					onClicked: {SufficitOAuthCpp.login()}
					Accessible.name: text
				}

				Rectangle {
					id: ramalAssignmentPanel
					visible: SufficitOAuthCpp.waitingForRamal
                    Layout.preferredWidth: Utils.getSizeWithScreenRatio(445)
                    Layout.preferredHeight: assignmentContent.implicitHeight + Utils.getSizeWithScreenRatio(40)
					color: DefaultStyle.grey_100
                    radius: Utils.getSizeWithScreenRatio(15)

					ColumnLayout {
						id: assignmentContent
						anchors.fill: parent
                        anchors.margins: Utils.getSizeWithScreenRatio(20)
                        spacing: Utils.getSizeWithScreenRatio(12)

						RowLayout {
                            spacing: Utils.getSizeWithScreenRatio(10)

							Rectangle {
                                Layout.preferredWidth: Utils.getSizeWithScreenRatio(28)
                                Layout.preferredHeight: Utils.getSizeWithScreenRatio(28)
                                radius: width / 2
                                color: DefaultStyle.success_500_main

								EffectImage {
									anchors.centerIn: parent
                                    imageSource: AppIcons.check
                                    imageWidth: Utils.getSizeWithScreenRatio(16)
                                    imageHeight: Utils.getSizeWithScreenRatio(16)
                                    colorizationColor: DefaultStyle.grey_0
								}
							}

							Text {
                                //: Status shown after the Sufficit sign-in succeeds.
                                text: qsTr("Sign-in complete")
                                color: DefaultStyle.success_700
								font: Typography.p2l
                                Accessible.name: text
							}
						}

						Text {
                            Layout.fillWidth: true
                            //: Heading asking the user to assign a phone extension (ramal) to this device.
                            text: qsTr("Choose this phone's ramal")
                            color: DefaultStyle.main2_900
							font: Typography.h3
                            wrapMode: Text.WordWrap
							Accessible.name: text
						}

						Text {
                            Layout.fillWidth: true
                            //: Instructions shown while this device waits for a ramal assignment.
                            text: qsTr("Open My Provisioning, select a ramal for this device, and keep the app open. Configuration will continue automatically.")
                            color: DefaultStyle.main2_600
							font: Typography.p1
                            wrapMode: Text.WordWrap
							Accessible.name: text
						}

						Text {
                            Layout.fillWidth: true
                            //: Displays the web address where the user can choose a ramal.
                            text: qsTr("Access: %1").arg(mainItem.ramalAssignmentUrl)
                            color: DefaultStyle.main2_700
							font: Typography.p2
                            wrapMode: Text.WrapAnywhere
                            Accessible.name: text
						}

						BigButton {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Utils.getSizeWithScreenRatio(47)
                            //: Opens the customer self-service page used to choose a ramal.
                            text: qsTr("Open My Provisioning")
                            icon.source: AppIcons.arrowSquareOut
							style: ButtonStyle.main
                            onClicked: Qt.openUrlExternally(mainItem.ramalAssignmentUrl)
                            Accessible.name: qsTr("Open My Provisioning to choose this phone's ramal")
						}

						RowLayout {
                            spacing: Utils.getSizeWithScreenRatio(8)

							BusyIndicator {
                                Layout.preferredWidth: Utils.getSizeWithScreenRatio(20)
                                Layout.preferredHeight: Utils.getSizeWithScreenRatio(20)
                                indicatorColor: DefaultStyle.main2_500_main
                                indicatorWidth: Utils.getSizeWithScreenRatio(16)
							}

							Text {
                                //: The app polls the server and proceeds as soon as a ramal is selected.
                                text: qsTr("Waiting for your selection...")
                                color: DefaultStyle.main2_500_main
								font: Typography.p1s
                                Accessible.name: text
							}
						}
					}
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
 
