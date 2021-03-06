/***********************************************************************
 *
 * Copyright (C) 2008, 2009, 2010, 2011, 2012 Graeme Gott <graeme@gottcode.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***********************************************************************/

#include "application.h"
#include "dictionary_manager.h"
#include "document.h"
#include "locale_dialog.h"
#include "session.h"
#include "sound.h"
#include "symbols_model.h"
#include "theme.h"

#if (QT_VERSION >= QT_VERSION_CHECK(5,0,0))
#include <QStandardPaths>
#else
#include <QDesktopServices>
#endif
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>
#include <QStringList>

int main(int argc, char** argv)
{
	Application app(argc, argv);
	if (app.isRunning()) {
		app.sendMessage(app.files().join(QLatin1String("\n")));
		return 0;
	}
	QString appdir = app.applicationDirPath();

	// Find application data dirs
	QStringList datadirs;
#if defined(Q_OS_MAC)
	QFileInfo portable(appdir + "/../../../Data");
	datadirs.append(appdir + "/../Resources");
#elif defined(Q_OS_UNIX)
	QFileInfo portable(appdir + "/Data");
	datadirs.append(DATADIR);
	datadirs.append(appdir + "/../share/focuswriter");
#else
	QFileInfo portable(appdir + "/Data");
	datadirs.append(appdir);
#endif

	// Handle portability
	QString userdir;
	if (portable.exists() && portable.isWritable()) {
		userdir = portable.absoluteFilePath();
		QSettings::setDefaultFormat(QSettings::IniFormat);
		QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, userdir + "/Settings");
	}

	// Set locations of fallback icons
	{
		QStringList paths = QIcon::themeSearchPaths();
		foreach (const QString& path, datadirs) {
			paths.prepend(path + "/icons");
		}
		QIcon::setThemeSearchPaths(paths);
	}

	// Find sounds
	foreach (const QString& path, datadirs) {
		if (QFile::exists(path + "/sounds/")) {
			Sound::setPath(path + "/sounds/");
			break;
		}
	}

	// Find unicode names
	foreach (const QString& path, datadirs) {
		if (QFile::exists(path + "/symbols.dat")) {
			SymbolsModel::setData(path + "/symbols.dat");
			break;
		}
	}

	// Load application language
	LocaleDialog::loadTranslator("focuswriter_", datadirs);

	// Find user data dir if not in portable mode
	if (userdir.isEmpty()) {
#if (QT_VERSION >= QT_VERSION_CHECK(5,0,0))
		userdir = QStandardPaths::writableLocation(QStandardPaths::DataLocation);
#else
		userdir = QDesktopServices::storageLocation(QDesktopServices::DataLocation);
#endif

		// Move user data to new location
		if (!QFile::exists(userdir)) {
#if defined(Q_OS_MAC)
			QString oldpath = QDir::homePath() + "/Library/Application Support/GottCode/FocusWriter/";
#elif defined(Q_OS_UNIX)
			QString oldpath = QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME"));
			if (oldpath.isEmpty()) {
				oldpath = QDir::homePath() + "/.local/share";
			}
			oldpath += "/focuswriter";
#else
			QString oldpath = QDir::homePath() + "/Application Data/GottCode/FocusWriter/";
#endif
			QDir dir(userdir + "/../");
			if (!dir.exists()) {
				dir.mkpath(dir.absolutePath());
			}
			if (QFile::exists(oldpath)) {
				QFile::rename(oldpath, userdir);
			}
		}
	}

	// Create base user data path
	QDir dir(userdir);
	if (!dir.exists()) {
		dir.mkpath(dir.absolutePath());
	}

	// Create cache path
	if (!dir.exists("Cache/Files")) {
		dir.mkpath("Cache/Files");
	}
	Document::setCachePath(dir.filePath("Cache/Files"));

	// Set sessions path
	if (!dir.exists("Sessions")) {
		dir.mkdir("Sessions");
	}
	Session::setPath(dir.absoluteFilePath("Sessions"));

	// Set themes path
	if (!dir.exists("Themes")) {
		if (dir.exists("themes")) {
			dir.rename("themes", "Themes");
		} else {
			dir.mkdir("Themes");
		}
	}
	if (!dir.exists("Themes/Images")) {
		dir.mkdir("Themes/Images");
	}
	Theme::setPath(dir.absoluteFilePath("Themes"));

	// Set dictionary paths
	if (!dir.exists("Dictionaries")) {
		if (dir.exists("dictionaries")) {
			dir.rename("dictionaries", "Dictionaries");
		} else {
			dir.mkdir("Dictionaries");
		}
	}
	DictionaryManager::setPath(dir.absoluteFilePath("Dictionaries"));

	// Create theme from old settings
	if (QDir(Theme::path(), "*.theme").entryList(QDir::Files).isEmpty()) {
		QSettings settings;
		Theme theme;
		theme.setName(Session::tr("Default"));

		theme.setBackgroundType(settings.value("Background/Position", theme.backgroundType()).toInt());
		theme.setBackgroundColor(settings.value("Background/Color", theme.backgroundColor()).toString());
		theme.setBackgroundImage(settings.value("Background/Image").toString());
		settings.remove("Background");

		theme.setForegroundColor(settings.value("Page/Color", theme.foregroundColor()).toString());
		theme.setForegroundWidth(settings.value("Page/Width", theme.foregroundWidth()).toInt());
		theme.setForegroundOpacity(settings.value("Page/Opacity", theme.foregroundOpacity()).toInt());
		settings.remove("Page");

		theme.setTextColor(settings.value("Text/Color", theme.textColor()).toString());
		theme.setTextFont(settings.value("Text/Font", theme.textFont()).value<QFont>());
		settings.remove("Text");

		settings.setValue("ThemeManager/Theme", theme.name());
	}

	// Create main window
	if (!app.createWindow()) {
		return 0;
	}

	// Browse to documents after command-line specified documents have been loaded
	QDir::setCurrent(QSettings().value("Save/Location", QDir::homePath()).toString());

	return app.exec();
}
