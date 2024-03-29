/*
 * stockPred.cpp
 *
 *  Created on: 23-Sep-2019
 *      Author: Shaikat Majumdar
 */

#include <fstream>
#include <iostream>
#include <memory>
#include <thread>

#include "MinMaxScaler.hpp"
#include "NetworkTrainer.hpp"
#include "StockPrices.hpp"

#include "NetworkConstants.hpp"
#include "RequestHandler.hpp"
#include "csv.h"

namespace
{
  class StockNetworkTrainer : public NetworkTrainer
  {
  public:
    StockNetworkTrainer(const std::string &fileName,
                        const std::string &companyName,
                        const MinMaxScaler<float> &minmaxScaler,
                        const std::vector<std::string> allDates)
        : NetworkTrainer(fileName, companyName),
          minMaxScaler{minmaxScaler}, allDates{allDates} {}

    virtual void dataWriter(const std::string &,
                            const std::vector<float> &) override
    {

      /*std::ofstream fileHandle(logFile, std::ios::trunc);
      fileHandle << "date,price\n";
      if (fileHandle.good()) {
        for (size_t idx = 0; idx < tensorData.size(); ++idx) {
          fileHandle << allDates.at(idx) << "," << minMaxScaler(tensorData[idx])
                     << '\n';
        }
      }
      fileHandle.close();*/
    }

  private:
    std::string fileName;
    const MinMaxScaler<float> &minMaxScaler;
    std::vector<std::string> allDates;
  };

  void updateConfig(const std::string &configFileName,
                    const std::string &stockSymbol,
                    const std::string &stockName = "")
  {
    std::ifstream testFileHandle(configFileName);
    bool isPresent = testFileHandle.good();
    testFileHandle.close();

    std::ofstream fileHandle(configFileName, std::ios::out | std::ios::app);

    if (!isPresent)
    {
      fileHandle << "Symbol,Company\n";
    }
    fileHandle << stockSymbol << ',' << stockName << '\n';
    fileHandle.close();
  }

  std::pair<std::string, std::string>
  getLastStock(const std::string &configFileName)
  {
    std::string symbol = "", companyName = "";

    std::cout << "Reading last trained stock from " << configFileName << '\n';
    try
    {
      io::CSVReader<2> in(configFileName);
      in.read_header(io::ignore_extra_column, "Symbol", "Company");
      while (in.read_row(symbol, companyName))
        ;
    }
    catch (...)
    {
    }

    return std::make_pair(symbol, companyName);
  }

  bool NetworkTrainerFacade(const std::string &stockSymbol,
                            const std::string &companyName = "")
  {

    MinMaxScaler<float> minmaxScaler;
    StockPrices stockData(minmaxScaler);
    if (stockData.loadTimeSeries(stockSymbol))
    {
      std::cout << stockSymbol << ":" << companyName
                << " has one or more bad entries\n";
      return false;
    }

    stockData.normalizeData();
    stockData.reshapeSeries(NetworkConstants::kSplitRatio,
                            NetworkConstants::kPrevSamples);

    auto trainData = stockData.getTrainData();
    auto testData = stockData.getTestData();

    const auto &x_train = std::get<0>(trainData);
    const auto &y_train = std::get<1>(trainData);

    const auto &x_test = std::get<0>(testData);
    const auto &y_test = std::get<1>(testData);

    // Record this stock for front end to update
    updateConfig(NetworkConstants::kRootFolder + "stock_train.csv", stockSymbol,
                 companyName);

    std::shared_ptr<NetworkTrainer> model = std::make_shared<StockNetworkTrainer>(
        stockSymbol, companyName, minmaxScaler, std::get<2>(trainData));

    model->dataWriter(NetworkConstants::kRootFolder + stockSymbol + "_train.csv",
                      y_train);

    (void)model->fit(x_train, y_train, x_test, y_test);

    return true;
  }
} // namespace

int main(int argc, char **argv)
{

  if (argc >= 2)
  {
    std::string stockParam = argv[1];
    bool testingMode = false;
    bool singleTraining = false;
    if (stockParam.find("testMode") != std::string::npos)
    {
      testingMode = true;
      singleTraining = false;
    }
    else if (stockParam.find("trainMode") != std::string::npos)
    {
      testingMode = false;
      singleTraining = false;
    }
    else if (stockParam.find("BOM") != std::string::npos)
    {
      testingMode = false;
      singleTraining = true;
    }

    if (testingMode && !singleTraining)
    {
      RequestHandler reqHandler;
      reqHandler.setupService(std::make_shared<StockPredictor>());

      std::thread t([&reqHandler]()
                    { reqHandler.run(); });
      t.join();
      return 0;
    }
    else if (!testingMode && !singleTraining)
    {
      std::cout << "Missing Stock Symbol...reading top 100 BSE stocks\n";
      const std::string bse100File =
          NetworkConstants::kRootFolder + "BSE100.csv";
      std::string stockSymbol;
      std::string companyName;
      auto lastUnderTrainStock =
          getLastStock(NetworkConstants::kRootFolder + "stock_train.csv");

      bool flag = false;
      io::CSVReader<2> in(bse100File);
      in.read_header(io::ignore_extra_column, "Symbol", "Name");

      while (argc >= 1 && in.read_row(stockSymbol, companyName))
      {

        if (!flag && !lastUnderTrainStock.first.empty() &&
            lastUnderTrainStock.first != stockSymbol)
        {
          std::cout << "Already Trained " << companyName << "..Skipping\n";
          continue;
        }
        else
        {
          if (!NetworkTrainerFacade(stockSymbol, companyName))
          {
            continue;
          }
          flag = true;
        }
      }
    }
    else
    {
      (void)NetworkTrainerFacade(argv[1], argv[2]);
    }
  }
  else
  {
    std::cout << "\nUsage :" << argv[0]
              << " testMode | trainMode | BSEStockSymbol\n";
  }

  return 0;
}
