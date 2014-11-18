// Copyright (C) 2014 Oliver Schulz <oschulz@mpp.mpg.de>

// This is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or
// (at your option) any later version.
//
// This software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.


#include "rootiobrics.h"

#include <TH1.h>
#include <TEntryList.h>
#include <TTree.h>
#include <TGraph2D.h>
#include <TEfficiency.h>
#include <TEveVSD.h>
#include <TGenericClassInfo.h>
#include <TEventList.h>
#include <TDSet.h>
#include <TClass.h>
#include <TSystemDirectory.h>
#include <TChain.h>

#include "logging.h"
#include "RootIO.h"
#include "TypeReflection.h"

using namespace std;


namespace dbrx {


void TTreeIterBric::Entry::connectBranches(Bric* contextBric, TTree* inputTree) {
	for (auto &elem: m_outputs) {
		OutputTerminal *terminal = elem.second;
		dbrx_log_debug("Connecting TTree branch \"%s\" in \"%s\"", terminal->name(), absolutePath());
		RootIO::inputValueFrom(terminal->value(), inputTree, terminal->name().toString());
	}
}


void TTreeIterBric::processInput() {
	dbrx_log_debug("Bric \"%s\" opens TTree \"%s\" from file \"%s\"", absolutePath(), treeName.get(), fileName.get());
	m_chain = std::unique_ptr<TChain>(new TChain(treeName->c_str()));
	m_chain->Add(fileName->c_str());

	m_chain->SetCacheSize(cacheSize);
	m_chain->SetBranchStatus("*", false);

	entry.connectBranches(this, m_chain.get());

	index = -1;
	size = m_chain->GetEntries();
}


bool TTreeIterBric::nextOutput() {
	if (index.get() < size.get()) {
		m_chain->GetEntry(index++);
		return true;
	} else return false;
}



const PropKey RootFileWriter::s_thisDirName(".");


void RootFileWriter::ContentGroup::connectInputs() {
	dbrx_log_trace("Creating and connecting dynamic inputs of bric \"%s\" and all inner brics", absolutePath());
	if (m_inputsConnected) throw logic_error("Can't connect already connected inputs in bric \"%s\""_format(absolutePath()));

	size_t nextInputIdx = 0;
	m_sourceInfos.clear();

	for (const PropPath &sourcePath: m_content) {
		InputTerminal *input = connectInputToSiblingOrUp(*this, nextInputIdx++, sourcePath);

		if (! TypeReflection(typeid(TNamed)).isPtrAssignableFrom(input->value().typeInfo()) )
			throw logic_error("Source terminal \"%s\" used for input in \"%s\" is not of type TNamed"_format(input->srcTerminal()->absolutePath(), absolutePath()));

		dbrx_log_trace("Triggering input from terminal \"%s\" on output of bric \"%s\" in \"%s\"", input->srcTerminal()->absolutePath(), input->effSrcBric()->absolutePath(), absolutePath());
		m_sourceInfos[input->effSrcBric()].inputs.push_back(input);
	}

	for (const auto& brics: m_brics) connectInputsOn(*brics.second);
	for (const auto& brics: m_brics) updateDepsOn(*brics.second);
}


void RootFileWriter::ContentGroup::initTDirectory() {
	// Nothing to do, TFiles and TDirectories will be created later
}


RootFileWriter::ContentGroup& RootFileWriter::ContentGroup::subGroup(PropKey name) {
	if (hasComponent(name)) {
		return dynamic_cast<ContentGroup&>(getBric(name));
	} else {
		dbrx_log_trace("Creating new sub-group \"%s\" in content group \"%s\""_format(name, absolutePath()));
		return dynamic_cast<ContentGroup&>(*addDynBric(unique_ptr<ContentGroup>(new ContentGroup(m_writer, this, name))));
	}
}


void RootFileWriter::ContentGroup::processInput() {
	TempChangeOfTDirectory tDirChange(localTDirectory());

	for (auto &si: m_sourceInfos) {
		const Bric* source = si.first;
		SourceInfo &info = si.second;
		if (info.inputCounter < outputCounterOn(*source)) {
			info.inputCounter = outputCounterOn(*source);
			for (const Terminal* input: info.inputs) {
				const TNamed *inputObject = (const TNamed*)input->value().untypedPtr();
				TNamed *outputObject = (TNamed*) inputObject->Clone();
				dbrx_log_trace("Writing object \"%s\" to content group \"%s\"", outputObject->GetName(), absolutePath());
				writeObject(outputObject);
			}
		}
	}
	for (auto &entry: m_brics) dynamic_cast<ContentGroup*>(entry.second)->processInput();
}


void RootFileWriter::ContentGroup::addContent(const PropVal &content) {
	if (content.isProps()) for (const auto &entry: content.asProps()) {
		const auto &dirName = entry.first;
		const auto &dirContent = entry.second;
		if (dirName == s_thisDirName) addContent(dirContent);
		else subGroup(dirName).addContent(dirContent);
	} else {
		for (const auto pv: content) {
			PropPath sourcePath(pv);
			if (sourcePath.empty()) throw invalid_argument("Invalid empty source in configuration for \"%s\""_format(absolutePath()));
			addContent(sourcePath);
		}
	}
}


void RootFileWriter::ContentGroup::addContent(const PropPath &sourcePath) {
	BricComponent* sourceComponent = &m_writer->getComponentRelToSiblings(sourcePath);

	if (dynamic_cast<Bric*>(sourceComponent)) {
		Bric &sourceBric = dynamic_cast<Bric&>(*sourceComponent);
		auto &sourceOutputs = sourceBric.outputs();
		dbrx_log_trace("Adding all outputs of Bric \"%s\" to content group \"%s\"", sourceBric.absolutePath(), absolutePath());
		if (sourceOutputs.size() >= 1) {
			for (auto& elem: sourceOutputs) {
				addContent(sourcePath % elem.second->name());
			}
		} else {
			dbrx_log_warn("Source \"%s\" for content group \"%s\" has no outputs", sourceBric.absolutePath(), absolutePath());
		}
	} else if (dynamic_cast<Bric::Terminal*>(sourceComponent)) {
		m_content.push_back(sourcePath);
		dbrx_log_trace("Added source terminal \"%s\" to content group \"%s\"", sourcePath, absolutePath());
	} else {
		throw logic_error("BricComponent \"%s\" is neither Bric nor Terminal"_format(sourceComponent->absolutePath()));
	}
}


void RootFileWriter::ContentGroup::openOutput() {
	for (auto &entry: m_sourceInfos) { entry.second.inputCounter = 0; }

	if (isTopGroup()) {
		const char *fileName = m_writer->fileName->c_str();
		const char *fileTitle = m_writer->title->c_str();
		dbrx_log_debug("Creating TFile \"%s\" with title \"%s\" in top content group \"%s\""_format(fileName, fileTitle, absolutePath()));
		TFile *tfile = TFile::Open(fileName, "recreate", fileTitle);
		if (tfile == nullptr) throw runtime_error("Could not create TFile \"%s\""_format(fileName));
		m_tDirectory = unique_ptr<TFile>(tfile);
	} else {
		const char *subDirName = name().toString().c_str();
		dbrx_log_trace("Creating new sub-directory \"%s/%s\" in output file in content group \"%s\""_format(parent().localTDirectory()->GetPath(), subDirName, absolutePath()));

		// Note: Have to use TDirectory::mkdir to create subdirs inside TFiles, "new TDirectory" doesn't work:
		m_tDirectory = unique_ptr<TDirectory>(parent().localTDirectory()->mkdir(subDirName, subDirName));
	}

	for (auto &entry: m_brics) dynamic_cast<ContentGroup*>(entry.second)->openOutput();
}


void RootFileWriter::ContentGroup::closeOutput() {
	for (auto &entry: m_brics) dynamic_cast<ContentGroup*>(entry.second)->closeOutput();

	if (isTopGroup()) {
		// Top group owns TFile - write, close and delete it:
		dbrx_log_debug("Closing TFile \"%s\" in top content group \"%s\""_format(localTDirectory()->GetName(), absolutePath()));
		m_tDirectory->Write();
		m_tDirectory->Close();
		m_tDirectory.reset();
	} else {
		// Sub-directory belongs to output file in top group, so just release it:
		m_tDirectory.release();
	}
}


RootFileWriter::ContentGroup::ContentGroup(RootFileWriter *writer, Bric *parentBric, PropKey groupName)
	: DynInputGroup(parentBric, groupName), m_writer(writer) {}



void RootFileWriter::writeObject(TNamed *obj) {
	if (string(obj->GetName()).empty())
		throw invalid_argument("Refusing to add object with empty name to TDirectory");

	bool autoAdded = (
		TH1::AddDirectoryStatus() &&
			(dynamic_cast<TH1*>(obj) != 0)
	) || (
		TDirectory::AddDirectoryStatus() && (
			dynamic_cast<TEntryList*>(obj) != 0 ||
			dynamic_cast<TTree*>(obj) != 0 ||
			dynamic_cast<TGraph2D*>(obj) != 0 ||
			dynamic_cast<TEfficiency*>(obj) != 0 ||
			dynamic_cast<TEveVSD*>(obj) != 0 ||
			dynamic_cast<ROOT::TGenericClassInfo*>(obj) != 0 ||
			dynamic_cast<TEventList*>(obj) != 0 ||
			dynamic_cast<TDSet*>(obj) != 0 ||
			dynamic_cast<TClass*>(obj) != 0 ||
			dynamic_cast<TSystemDirectory*>(obj) != 0 ||
			dynamic_cast<TChain*>(obj) != 0
		)
	);

	if (!autoAdded) obj->Write();
}


void RootFileWriter::connectInputs() {
	dbrx_log_trace("Setting up content groups for bric \"%s\"", absolutePath());
	inputs.addContent(content);

	AsyncReducerBric::connectInputs();
}


void RootFileWriter::newReduction() {
	inputs.openOutput();
}


void RootFileWriter::processInput() {
	inputs.processInput();
}


void RootFileWriter::finalizeReduction() {
	inputs.closeOutput();
}


} // namespace dbrx
