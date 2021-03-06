#include "app.hpp"
#include "engine.hpp"
#include "plugin.hpp"
#include "window.hpp"
#include "settings.hpp"
#include "asset.hpp"
#include <map>
#include <algorithm>
#include "osdialog.h"


namespace rack {


struct ModuleContainer : Widget {
	/*void draw(NVGcontext *vg) override {
		// Draw shadows behind each ModuleWidget first, so the shadow doesn't overlap the front.
		/*for (Widget *child : children) {
			if (!child->visible)
				continue;
			nvgSave(vg);
			nvgTranslate(vg, child->box.pos.x, child->box.pos.y);
			ModuleWidget *w = dynamic_cast<ModuleWidget*>(child);
			w->drawShadow(vg);
			nvgRestore(vg);
		}*

		Widget::draw(vg);
	}*/
};


RackWidget::RackWidget() {
	rails = new FramebufferWidget();
	rails->box.size = Vec();
	rails->oversample = 1.0;
	{
		RackRail *rail = new RackRail();
		rail->box.size = Vec();
		rails->addChild(rail);
	}
	// addChild(rails);

	moduleContainer = new ModuleContainer();
	addChild(moduleContainer);

	wireContainer = new WireContainer();
	addChild(wireContainer);
}

RackWidget::~RackWidget() {
	// Without this, our lights vector will be destroyed first,
	// and only then superclass will eventually destroy ModuleWidget instances
	// that need the lights vector.
	clearChildren();
}

void RackWidget::clear() {
	wireContainer->activeWire = NULL;
	wireContainer->clearChildren();
	moduleContainer->clearChildren();

	gRackScene->scrollWidget->offset = Vec(0, 0);
}

void RackWidget::reset() {
	if (osdialog_message(OSDIALOG_INFO, OSDIALOG_OK_CANCEL, "Clear your patch and start over?")) {
		clear();
		// Fails silently if file does not exist
		loadPatch(assetLocal("template.vcv"));
		lastPath = "";
	}
}

void RackWidget::openDialog() {
	std::string dir = lastPath.empty() ? assetLocal("") : stringDirectory(lastPath);
	char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
	if (path) {
		loadPatch(path);
		lastPath = path;
		free(path);
	}
}

void RackWidget::saveDialog() {
	if (!lastPath.empty()) {
		savePatch(lastPath);
	}
	else {
		saveAsDialog();
	}
}

void RackWidget::saveAsDialog() {
	std::string dir = lastPath.empty() ? assetLocal("") : stringDirectory(lastPath);
	char *path = osdialog_file(OSDIALOG_SAVE, dir.c_str(), "Untitled.vcv", NULL);

	if (path) {
		std::string pathStr = path;
		free(path);
		std::string extension = stringExtension(pathStr);
		if (extension.empty()) {
			pathStr += ".vcv";
		}

		savePatch(pathStr);
		lastPath = pathStr;
	}
}

void RackWidget::savePatch(std::string path) {
	info("Saving patch %s", path.c_str());
	json_t *rootJ = toJson();
	if (!rootJ)
		return;

	FILE *file = fopen(path.c_str(), "w");
	if (file) {
		json_dumpf(rootJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		fclose(file);
	}

	json_decref(rootJ);
}

void RackWidget::loadPatch(std::string path) {
	info("Loading patch %s", path.c_str());
	FILE *file = fopen(path.c_str(), "r");
	if (!file) {
		// Exit silently
		return;
	}

	json_error_t error;
	json_t *rootJ = json_loadf(file, 0, &error);
	if (rootJ) {
		clear();
		fromJson(rootJ);
		json_decref(rootJ);
	}
	else {
		std::string message = stringf("JSON parsing error at %s %d:%d %s\n", error.source, error.line, error.column, error.text);
		osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
	}

	fclose(file);
}

void RackWidget::revert() {
	if (lastPath.empty())
		return;
	if (osdialog_message(OSDIALOG_INFO, OSDIALOG_OK_CANCEL, "Revert your patch to the last saved state?")) {
		loadPatch(lastPath);
	}
}

void RackWidget::disconnect() {
	for (Widget *w : moduleContainer->children) {
		ModuleWidget *moduleWidget = dynamic_cast<ModuleWidget*>(w);
		assert(moduleWidget);
		moduleWidget->disconnect();
	}
}

json_t *RackWidget::toJson() {
	// root
	json_t *rootJ = json_object();

	// version
	json_t *versionJ = json_string(gApplicationVersion.c_str());
	json_object_set_new(rootJ, "version", versionJ);

	// modules
	json_t *modulesJ = json_array();
	std::map<ModuleWidget*, int> moduleIds;
	int moduleId = 0;
	for (Widget *w : moduleContainer->children) {
		ModuleWidget *moduleWidget = dynamic_cast<ModuleWidget*>(w);
		assert(moduleWidget);
		moduleIds[moduleWidget] = moduleId;
		moduleId++;
		// module
		json_t *moduleJ = moduleWidget->toJson();
		json_array_append_new(modulesJ, moduleJ);
	}
	json_object_set_new(rootJ, "modules", modulesJ);

	// wires
	json_t *wires = json_array();
	for (Widget *w : wireContainer->children) {
		WireWidget *wireWidget = dynamic_cast<WireWidget*>(w);
		assert(wireWidget);
		// Only serialize WireWidgets connected on both ends
		if (!(wireWidget->outputPort && wireWidget->inputPort))
			continue;
		// wire
		json_t *wire = wireWidget->toJson();

		// Get the modules at each end of the wire
		ModuleWidget *outputModuleWidget = wireWidget->outputPort->getAncestorOfType<ModuleWidget>();
		assert(outputModuleWidget);
		int outputModuleId = moduleIds[outputModuleWidget];

		ModuleWidget *inputModuleWidget = wireWidget->inputPort->getAncestorOfType<ModuleWidget>();
		assert(inputModuleWidget);
		int inputModuleId = moduleIds[inputModuleWidget];

		// Get output/input ports
		int outputId = wireWidget->outputPort->portId;
		int inputId = wireWidget->inputPort->portId;

		json_object_set_new(wire, "outputModuleId", json_integer(outputModuleId));
		json_object_set_new(wire, "outputId", json_integer(outputId));
		json_object_set_new(wire, "inputModuleId", json_integer(inputModuleId));
		json_object_set_new(wire, "inputId", json_integer(inputId));

		json_array_append_new(wires, wire);
	}
	json_object_set_new(rootJ, "wires", wires);

	return rootJ;
}

void RackWidget::fromJson(json_t *rootJ) {
	std::string message;

	// version
	std::string version;
	json_t *versionJ = json_object_get(rootJ, "version");
	if (versionJ) {
		version = json_string_value(versionJ);
	}

	// Detect old patches with ModuleWidget::params/inputs/outputs indices.
	// (We now use Module::params/inputs/outputs indices.)
	int legacy = 0;
	if (stringStartsWith(version, "0.3.") || stringStartsWith(version, "0.4.") || stringStartsWith(version, "0.5.") || version == "" || version == "dev") {
		legacy = 1;
		message += "This patch was created with Rack 0.5 or earlier. Saving it will convert it to a Rack 0.6+ patch.\n\n";
	}
	if (legacy) {
		info("Loading patch using legacy mode %d", legacy);
	}

	// modules
	std::map<int, ModuleWidget*> moduleWidgets;
	json_t *modulesJ = json_object_get(rootJ, "modules");
	if (!modulesJ) return;
	size_t moduleId;
	json_t *moduleJ;
	json_array_foreach(modulesJ, moduleId, moduleJ) {
		// Add "legacy" property if in legacy mode
		if (legacy) {
			json_object_set(moduleJ, "legacy", json_integer(legacy));
		}

		json_t *pluginSlugJ = json_object_get(moduleJ, "plugin");
		if (!pluginSlugJ) continue;
		json_t *modelSlugJ = json_object_get(moduleJ, "model");
		if (!modelSlugJ) continue;
		std::string pluginSlug = json_string_value(pluginSlugJ);
		std::string modelSlug = json_string_value(modelSlugJ);

		Model *model = pluginGetModel(pluginSlug, modelSlug);
		if (!model) {
			message += stringf("Could not find module \"%s\" of plugin \"%s\"\n", modelSlug.c_str(), pluginSlug.c_str());
			continue;
		}

		// Create ModuleWidget
		ModuleWidget *moduleWidget = model->createModuleWidget();
		assert(moduleWidget);
		moduleWidget->fromJson(moduleJ);
		gRackWidget->addModule(moduleWidget);
		moduleWidgets[moduleId] = moduleWidget;
	}

	// wires
	json_t *wiresJ = json_object_get(rootJ, "wires");
	if (!wiresJ) return;
	size_t wireId;
	json_t *wireJ;
	json_array_foreach(wiresJ, wireId, wireJ) {
		int outputModuleId = json_integer_value(json_object_get(wireJ, "outputModuleId"));
		int outputId = json_integer_value(json_object_get(wireJ, "outputId"));
		int inputModuleId = json_integer_value(json_object_get(wireJ, "inputModuleId"));
		int inputId = json_integer_value(json_object_get(wireJ, "inputId"));

		// Get module widgets
		ModuleWidget *outputModuleWidget = moduleWidgets[outputModuleId];
		if (!outputModuleWidget) continue;
		ModuleWidget *inputModuleWidget = moduleWidgets[inputModuleId];
		if (!inputModuleWidget) continue;

		// Get port widgets
		Port *outputPort = NULL;
		Port *inputPort = NULL;
		if (legacy && legacy <= 1) {
			// Legacy 1 mode
			// The index of the "ports" array is the index of the Port in the `outputs` and `inputs` vector.
			outputPort = outputModuleWidget->outputs[outputId];
			inputPort = inputModuleWidget->inputs[inputId];
		}
		else {
			for (Port *port : outputModuleWidget->outputs) {
				if (port->portId == outputId) {
					outputPort = port;
					break;
				}
			}
			for (Port *port : inputModuleWidget->inputs) {
				if (port->portId == inputId) {
					inputPort = port;
					break;
				}
			}
		}
		if (!outputPort || !inputPort)
			continue;

		// Create WireWidget
		WireWidget *wireWidget = new WireWidget();
		wireWidget->fromJson(wireJ);
		wireWidget->outputPort = outputPort;
		wireWidget->inputPort = inputPort;
		wireWidget->updateWire();
		// Add wire to rack
		wireContainer->addChild(wireWidget);
	}

	// Display a message if we have something to say
	if (!message.empty()) {
		osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
	}
}

void RackWidget::addModule(ModuleWidget *m) {
	moduleContainer->addChild(m);

	// To update dirty flag for all ancestors
	m->dirty = true;

	//TODO: this is temporary until we handle individual dimension chages that resizable modules do in fromJson()
	m->onResize();

	m->create();
}

void RackWidget::deleteModule(ModuleWidget *m) {
	m->_delete();
	moduleContainer->removeChild(m);
}

void RackWidget::cloneModule(ModuleWidget *m) {
	// Create new module from model
	ModuleWidget *clonedModuleWidget = m->model->createModuleWidget();
	// JSON serialization is the most straightforward way to do this
	json_t *moduleJ = m->toJson();
	clonedModuleWidget->fromJson(moduleJ);
	json_decref(moduleJ);
	Rect clonedBox = clonedModuleWidget->box;
	clonedBox.pos = m->box.pos;
	requestModuleBoxNearest(clonedModuleWidget, clonedBox);
	addModule(clonedModuleWidget);
}

bool RackWidget::requestModuleBox(ModuleWidget *m, Rect box) {
	if (box.pos.x < 0 || box.pos.y < 0)
		return false;

	for (Widget *child2 : moduleContainer->children) {
		if (m == child2) continue;
		if (box.intersects(child2->box)) {
			return false;
		}
	}
	// if (!box.size.isEqual(m->box.size))
	// 	if (m->staticPanel)
	// 		m->staticPanel->dirty = true;
	m->box = box;
	return true;
}

static inline Widget* moduleIntersectingBox(Widget *container, Widget *self, Rect box) {
	for (Widget *child2 : container->children) {
		if (self == child2) continue;
		if (box.intersects(child2->box))
			return child2;
	}

	return NULL;
}

bool RackWidget::requestModuleBoxNearest(ModuleWidget *m, Rect box) {
	int x0 = std::max(0.f, roundf(box.pos.x / RACK_GRID_WIDTH) * RACK_GRID_WIDTH);
	box.pos.y = std::max(0.f, roundf(box.pos.y / RACK_GRID_HEIGHT) * RACK_GRID_HEIGHT);

	float left, right;
	for (left = x0; left >= 0;) {
		box.pos.x = left;
		Widget *w = moduleIntersectingBox(moduleContainer, m, box);
		if (!w)
			break;
		left = w->box.pos.x - m->box.size.x;
	}

	for (right = x0; ;) {
		box.pos.x = right;
		Widget *w = moduleIntersectingBox(moduleContainer, m, box);
		if (!w)
			break;
		right = w->box.pos.x + w->box.size.x;
	}

	float mouseX = gMousePos.x + gRackScene->scrollWidget->offset.x;
	if (left >= 0 && fabsf(mouseX - (left+box.size.x)) <= fabsf(mouseX - right))
		box.pos.x = left;
	else
		box.pos.x = right;

	m->box = box;

	return true;
}

void RackWidget::step() {
	// Expand size to fit modules
	Vec moduleSize = moduleContainer->getChildrenBoundingBox().getBottomRight();
	// We assume that the size is reset by a parent before calling step(). Otherwise it will grow unbounded.
	box.size = box.size.max(moduleSize);

	// Adjust size and position of rails
	Widget *rail = rails->children.front();
	Rect bound = getViewport(Rect(Vec(), box.size));
	if (!rails->box.contains(bound)) {
		Vec cellMargin = Vec(20, 1);
		rails->box.pos = bound.pos.div(RACK_GRID_SIZE).floor().minus(cellMargin).mult(RACK_GRID_SIZE);
		rails->box.size = bound.size.plus(cellMargin.mult(RACK_GRID_SIZE).mult(2));
		rails->dirty = true;

		rail->box.size = rails->box.size;
	}

	// Autosave every 15 seconds
	if (gGuiFrame % (60 * 15) == 0) {
		savePatch(assetLocal("autosave.vcv"));
		settingsSave(assetLocal("settings.json"));
	}

	Widget::step();
}

void RackWidget::draw(NVGcontext *vg) {
	// rails->draw(vg);

	float zoom = 1./gRackScene->zoomWidget->zoom;
	Vec pos = parent->parent->box.pos.neg().mult(zoom);
	Vec size = parent->parent->parent->box.size.mult(zoom);

	for (Widget *child : moduleContainer->children) {
		ModuleWidget *mw = dynamic_cast<ModuleWidget*>(child);
		if (!mw->staticPanel)
			continue;

		if (child->box.pos.x-pos.x >= size.x || child->box.pos.y-pos.y >= size.y ||
			child->box.pos.x+child->box.size.x < pos.x || child->box.pos.y+child->box.size.y < pos.y)
			continue;

		nvgSave(vg);
		nvgTranslate(vg, child->box.pos.x, child->box.pos.y);
		mw->staticPanel->drawCachedOrFresh(vg);
		nvgRestore(vg);
	}

	for (Widget *child : moduleContainer->children) {
		if (child->box.pos.x-pos.x >= size.x || child->box.pos.y-pos.y >= size.y ||
			child->box.pos.x+child->box.size.x < pos.x || child->box.pos.y+child->box.size.y < pos.y)
			continue;

		nvgSave(vg);
		nvgTranslate(vg, child->box.pos.x, child->box.pos.y);
		child->drawCachedOrFresh(vg);
		nvgRestore(vg);
	}

	static int lightImage = 0;
	static unsigned char lightData[256*4];
	if (!lightImage) {
		// for (int i = 0; i < 256; i++)
		// {
		// 	lightData[i*4+0] = 255;
		// 	lightData[i*4+1] = 0;
		// 	lightData[i*4+2] = 0;
		// 	lightData[i*4+3] = 255;
		// }
		lightImage = nvgCreateImageRGBA(vg, 256, 1, NVG_IMAGE_NEAREST|NVG_IMAGE_PREMULTIPLIED, lightData);
	}

	nvgBeginPath(vg);
	nvgAllowMergeSubpaths(vg);

	int i = 0;
	for (LightWidget *light : lights) {
		light->step();
		// if (!child->visible)
		// 	continue;
		// light->draw(vg);
		// wire->needsRender = false;

		float radius = light->box.size.x / 2.0;

		// nvgBeginPath(vg);
		nvgCircle(vg, light->parent->box.pos.x+light->box.pos.x+radius, light->parent->box.pos.y+light->box.pos.y+radius, radius);
		nvgSubpathTexPos(vg, i/256., 0.5f);
		float a = light->color.a;
		float bga = 1. - light->color.a;
		lightData[i*4+0] = (light->bgColor.r * bga + light->color.r * a) * 255.;
		lightData[i*4+1] = (light->bgColor.g * bga + light->color.g * a) * 255.;
		lightData[i*4+2] = (light->bgColor.b * bga + light->color.b * a) * 255.;
		lightData[i*4+3] = (light->bgColor.a * bga + light->color.a) * 255.;
// printf("%p %f %f\n", light, light->color.r, light->color.g);

		// // Background
		// if (color.a < 1.f)
		// {
		// 	nvgFillColor(vg, bgColor);
		// 	nvgFill(vg);
		// }

		// Foreground
		// if (color.a)
		// {
		// 	nvgFillColor(vg, color);
		// 	nvgFill(vg);
		// }		
		i++;
	}

	// nvgFillColor(vg, nvgRGB(0x47, 0x18, 0xc9));
	nvgUpdateImage(vg, lightImage, lightData);
	nvgFillPaint(vg, nvgImagePattern(vg, 0, 0, 256, 1, 0.0, lightImage, 1.0));
	nvgFill(vg);

	wireContainer->draw(vg);
}

void RackWidget::onMouseMove(EventMouseMove &e) {
	OpaqueWidget::onMouseMove(e);
	lastMousePos = e.pos;
}

void RackWidget::onMouseDown(EventMouseDown &e) {
	Widget::onMouseDown(e);
	if (e.consumed)
		return;

	if (e.button == 1) {
		appModuleBrowserCreate();
	}
	e.consumed = true;
	e.target = this;
}

void RackWidget::onZoom(EventZoom &e) {
	rails->box.size = Vec();
	Widget::onZoom(e);
}


} // namespace rack
