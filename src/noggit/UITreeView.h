// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <string>
#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <noggit/UIButton.h>
#include <noggit/UIFrame.h>
#include <noggit/Directory.h>

class UIText;
class UITreeViewButton;

class UITreeView : public UIFrame, public boost::enable_shared_from_this<UITreeView>
{
public:
	typedef boost::shared_ptr<UITreeView> Ptr;
	typedef std::vector<UITreeView::Ptr> Others;

	Others _others;
private:
	UITreeView::Ptr mParent;
	std::vector<UIText*> mFiles;
	Directory::Ptr mMyDir;
	UITreeViewButton * mMyButton;
	UIText * mMyText;
	const std::string _directoryName;

	std::function<void(const std::string&)> mSelectFunction;

	bool mExpanded;

public:
	UITreeView(float pX, float pY, const std::string& directoryName, Directory::Ptr pDirectory, UITreeView::Ptr pParent, std::function<void(const std::string&)> pSelectFunction);

	void Expand();
	void Minimize();
	bool Expanded();
	void Toggle();

	void SetSelectFunction(void(*pSelectFunction)(const std::string&));
	const std::string& GetDirectoryName();
	UITreeView::Ptr GetParent()
	{
		return mParent;
	}

	void Move(int pEntries, UITreeView::Ptr pFrom);

	void render() const;

	UIFrame * processLeftClick(float mx, float my);
};

class UITreeViewButton : public UIButton
{
private:
	UITreeView::Ptr mTreeView;
public:
	UITreeViewButton(float x, float y, UITreeView::Ptr pTreeView);

	UIFrame::Ptr processLeftClick(float mx, float my);

	void SetClicked(bool pClicked);
};
