#include "GuiMoveToFolder.h"
#include "CollectionSystemManager.h"
#include "SystemConf.h"
#include "SystemData.h"
#include "platform.h"

#include "views/gamelist/IGameListView.h"
#include "views/gamelist/ISimpleGameListView.h"
#include "views/ViewController.h"

#include "guis/GuiMsgBox.h"
#include "guis/GuiTextEditPopupKeyboard.h"
#include "guis/GuiTextEditPopup.h"

template<class T>
T base_path(T const & path, T const & delims = "/\\")
{
  return path.substr(0,path.find_last_of(delims));
}

template<class T>
T base_name(T const & path, T const & delims = "/\\")
{
  return path.substr(path.find_last_of(delims) + 1);
}

template<class T>
T remove_extension(T const & filename)
{
  typename T::size_type const p(filename.find_last_of('.'));
  return p > 0 && p != T::npos ? filename.substr(0, p) : filename;
}

GuiMoveToFolder::GuiMoveToFolder(Window* window, FileData* file) :
  mWindow(window),
  mFile(file),
  GuiSettings(window, _("FILE ")+file->getName().c_str())
{
  auto theme = ThemeData::getMenuTheme();

  auto emuelec_folderopt_def = std::make_shared< OptionListComponent<std::string> >(mWindow, "CHOOSE FOLDER", false);

  if (file->getType() == GAME) {
    addEntry(_("MOVE GAME TO FOLDER"), true, [this, file, emuelec_folderopt_def]
    {
      std::string folderOption = (emuelec_folderopt_def->getSelected().empty()) ?
        SystemConf::getInstance()->get("folder_option") : emuelec_folderopt_def->getSelected();
      if (!folderOption.empty())
        moveToFolderGame(file, folderOption);
      close();
    });
  }

  makeFolderList(file, emuelec_folderopt_def);

  if (file->getType() == GAME) {
    addWithLabel(_("CHOOSE FOLDER"), emuelec_folderopt_def);
    const std::function<void()> saveFunc([emuelec_folderopt_def] {
      if (emuelec_folderopt_def->changed()) {
        std::string selectedfolder = emuelec_folderopt_def->getSelected();
        SystemConf::getInstance()->set("folder_option", selectedfolder);
        SystemConf::getInstance()->saveSystemConf();
      }
    });
    addSaveFunc(saveFunc);
    emuelec_folderopt_def->setSelectedChangedCallback(
      [emuelec_folderopt_def, saveFunc] (std::string val) {
        saveFunc();
      });
  }

	ComponentListRow row;
	auto createName = std::make_shared<TextComponent>(window, _("CREATE FOLDER"),
    theme->Text.font, theme->Text.color);
	row.addElement(createName, true);
  auto updateFN = [this, window, file, emuelec_folderopt_def](const std::string& newVal)
	{
		if (newVal.empty()) return;

    std::string path = base_path<std::string>(file->getPath()) + "/" + newVal;
		if (Utils::FileSystem::exists(path.c_str())) {
      window->pushGui(new GuiMsgBox(window, _("FOLDER EXISTS"), _("OK"), nullptr));
      return;
    }
    createFolder(file, path);
    makeFolderList(file, emuelec_folderopt_def);
	};

  row.makeAcceptInputHandler([this, window, file, updateFN]
	{
		if (Settings::getInstance()->getBool("UseOSK"))
			window->pushGui(new GuiTextEditPopupKeyboard(window, _("FOLDER NAME"), "", updateFN, false));
		else
			window->pushGui(new GuiTextEditPopup(window, _("FOLDER NAME"), "", updateFN, false));
	});

  addRow(row);
}

void GuiMoveToFolder::makeFolderList(FileData* file, std::shared_ptr< OptionListComponent<std::string>> optionList)
{
  std::vector<FolderData*> fds = getChildFolders(file->getParent());

  std::string folderoptionsS = SystemConf::getInstance()->get("folder_option");
  std::string basePath = file->getSystem()->getRootFolder()->getPath();
  size_t len = base_path(basePath).length()+1;
  std::string subpath = basePath;
  subpath.replace(0, len, "");

  optionList->clear();

  if (file->getParent()->getParent() != nullptr) {
    if (fds.size() == 0)
      folderoptionsS = basePath;

    optionList->add(subpath, basePath, folderoptionsS == basePath);
  }

  for (auto it = fds.begin(); it != fds.end(); it++) {
    FolderData* fd = *it;

    subpath = fd->getPath();
    subpath.replace(0, len, "");

    optionList->add(subpath, fd->getPath(), folderoptionsS == fd->getPath());
  }

  if (optionList->getSelectedIndex() == -1)
    optionList->selectFirstItem();
};

void GuiMoveToFolder::moveToFolderGame(FileData* file, const std::string& path)
{
	if (file->getType() != GAME)
		return;

	auto sourceFile = file->getSourceFileData();

	auto sys = sourceFile->getSystem();
	if (sys->isGroupChildSystem())
		sys = sys->getParentGroupSystem();

	CollectionSystemManager::get()->deleteCollectionFiles(sourceFile);

	auto view = ViewController::get()->getGameListView(sys, false);

	char cmdMvFile[1024];
  snprintf(cmdMvFile, sizeof(cmdMvFile), "mv \"%s\" \"%s\" 2>&1 | tee /emuelec/logs/mtf.log",
    sourceFile->getFullPath().c_str(), path.c_str());
  std::string strMvFile = cmdMvFile;
	system(strMvFile.c_str());

  FolderData* fd = file->getSystem()->getRootFolder();
  if (path != fd->getPath()) {
    fd = getFolderData(file->getParent(), base_name<std::string>(path.c_str()));
  }

  if (fd != nullptr) {
    std::string newPath = path+"/"+base_name<std::string>(file->getPath());
    FileData* newFile = new FileData(GAME, newPath, file->getSystem());
    newFile->setMetadata(file->getMetadata());

    fd->addChild(newFile);

    if (view != nullptr) {
      view.get()->remove(sourceFile);
      delete sourceFile;
    }
    else {
      sys->getRootFolder()->removeFromVirtualFolders(sourceFile);
    }
  }
}

std::vector<FolderData*> GuiMoveToFolder::getChildFolders(FolderData* folder)
{
  std::vector<FolderData*> fds;
  std::vector<FileData*> children = folder->getChildren();
  for (auto it = children.begin(); it != children.end(); it++) {
    if ((*it)->getType() == FOLDER)
      fds.push_back(dynamic_cast<FolderData*>(*it));
  }
  return fds;
}

FolderData* GuiMoveToFolder::getFolderData(FolderData* folder, const std::string& name)
{
  std::vector<FileData*> children = folder->getChildren();
  std::string name2;
  for (auto it = children.begin(); it != children.end(); it++) {
    name2 = base_name<std::string>((*it)->getPath());
    if ((*it)->getType() == FOLDER && name2 == name)
      return dynamic_cast<FolderData*>(*it);
  }
  return nullptr;
}

void GuiMoveToFolder::createFolder(FileData* file, const std::string& path)
{
  auto sourceFile = file->getSourceFileData();

  auto sys = sourceFile->getSystem();
	if (sys->isGroupChildSystem())
		sys = sys->getParentGroupSystem();
  auto view = ViewController::get()->getGameListView(sys, false);

  if (!Utils::FileSystem::exists(path.c_str())) {
		Utils::FileSystem::createDirectory(path.c_str());

    std::string showFoldersMode = Settings::getInstance()->getString("FolderViewMode");
    if (showFoldersMode == "always") {
      file->getParent()->addChild(new FolderData(path.c_str(), sys, false));
      ViewController::get()->reloadGameListView(sys);
    }
	}
}
