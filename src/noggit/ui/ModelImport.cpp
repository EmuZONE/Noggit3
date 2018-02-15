// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/ui/ModelImport.h>

#include <noggit/MapView.h>
#include <noggit/ModelInstance.h>
#include <noggit/Selection.h>
#include <noggit/WMOInstance.h>

#include <fstream>
#include <regex>
#include <string>

#include <QtWidgets/QFormLayout>

namespace noggit
{
  namespace ui
  {
    model_import::model_import (MapView* mapview)
      : QWidget (mapview, Qt::Tool | Qt::WindowStaysOnTopHint)
    {
      setWindowIcon (QIcon (":/icon"));
      auto layout (new QFormLayout (this));

      layout->addRow ("Filter", _textBox = new QLineEdit (this));
      connect ( _textBox, &QLineEdit::textChanged
              , [this]
                {
                  buildModelList();
                }
              );

      layout->addWidget (_list = new QListWidget (this));

      buildModelList();

      connect ( _list, &QListWidget::itemDoubleClicked
              , [mapview] (QListWidgetItem* item)
                {
                  mapview->selectModel (item->text ().toStdString());
                }
              );
    }

    void model_import::buildModelList()
    {
      _list->clear();

      QSettings settings;

      std::ifstream fileReader (settings.value ("project/import_file", "import.txt").toString().toStdString());
      std::string const filter
        (mpq::normalized_filename (_textBox->text().toStdString()));

      std::string line;
      while (std::getline (fileReader, line))
      {
        if (line.empty())
        {
          continue;
        }

        std::string path (mpq::normalized_filename (line));

        if (!filter.empty() && path.find (filter) == std::string::npos)
        {
          continue;
        }

        std::regex regex("[^\.]+\.(m2|wmo)"), wmo_group(".*_[0-9]{3}\.wmo");
        std::smatch match;

        if (std::regex_search(path, match, regex) && !regex_match(path, wmo_group))
        {
          _list->addItem (QString::fromStdString (match.str(0)));
        }        
      }
    }
  }
}
