#include "app.hpp"

namespace rack {


void WireContainer::setActiveWire(WireWidget *w) {
	if (activeWire) {
		removeChild(activeWire);
		delete activeWire;
		activeWire = NULL;
	}
	if (w) {
		if (w->parent == NULL)
			addChild(w);
		activeWire = w;
	}
}

void WireContainer::commitActiveWire() {
	if (!activeWire)
		return;

	if (activeWire->hoveredInputPort) {
		activeWire->inputPort = activeWire->hoveredInputPort;
		activeWire->hoveredInputPort = NULL;
	}
	if (activeWire->hoveredOutputPort) {
		activeWire->outputPort = activeWire->hoveredOutputPort;
		activeWire->hoveredOutputPort = NULL;
	}
	activeWire->updateWire();

	// Did it successfully connect?
	if (activeWire->wire) {
		// Make it permanent
		activeWire = NULL;
	}
	else {
		// Remove it
		setActiveWire(NULL);
	}
}

void WireContainer::removeTopWire(Port *port) {
	WireWidget *wire = getTopWire(port);
	if (wire) {
		removeChild(wire);
		delete wire;
	}
}

void WireContainer::removeAllWires(Port *port) {
	// As a convenience, de-hover the active wire so we don't attach them once it is dropped.
	if (activeWire) {
		if (activeWire->hoveredInputPort == port)
			activeWire->hoveredInputPort = NULL;
		if (activeWire->hoveredOutputPort == port)
			activeWire->hoveredOutputPort = NULL;
	}

	// Build a list of WireWidgets to delete
	std::list<WireWidget*> wires;

	for (Widget *child : children) {
		WireWidget *wire = dynamic_cast<WireWidget*>(child);
		assert(wire);
		if (wire->inputPort == port || wire->outputPort == port) {
			if (activeWire == wire) {
				activeWire = NULL;
			}
			// We can't delete from this list while we're iterating it, so add it to the deletion list.
			wires.push_back(wire);
		}
	}

	// Once we're done building the list, actually delete them
	for (WireWidget *wire : wires) {
		removeChild(wire);
		delete wire;
	}
}

WireWidget *WireContainer::getTopWire(Port *port) {
	for (auto it = children.rbegin(); it != children.rend(); it++) {
		WireWidget *wire = dynamic_cast<WireWidget*>(*it);
		assert(wire);
		// Ignore incomplete wires
		if (!(wire->inputPort && wire->outputPort))
			continue;
		if (wire->inputPort == port || wire->outputPort == port)
			return wire;
	}
	return NULL;
}

void WireContainer::draw(NVGcontext *vg) {
	//Widget::draw(vg);
// return;
		nvgSave(vg);
		float opacity = gToolbar->wireOpacitySlider->value / 100.0;
		nvgGlobalAlpha(vg, powf(opacity, 1.5));
		// nvgLineJoin(vg, NVG_ROUND);

		nvgBeginPath(vg);
	for (Widget *child : children) {
		// if (!child->visible)
		// 	continue;
		nvgSave(vg);
		nvgTranslate(vg, child->box.pos.x, child->box.pos.y);
		child->draw(vg);
		nvgRestore(vg);
	}

		nvgStrokeColor(vg, nvgRGB(0xc9, 0x18, 0x47));
		nvgStrokeWidth(vg, 4);
		nvgStroke(vg);
	nvgRestore(vg);

// return;
	// Wire plugs
	nvgBeginPath(vg);
	nvgAllowMergeSubpaths(vg);

	for (Widget *child : children) {
		// if (!child->visible)
		// 	continue;
		WireWidget *wire = dynamic_cast<WireWidget*>(child);
		assert(wire);
		wire->drawPlugs(vg);
		// wire->needsRender = false;
	}

	nvgFillColor(vg, nvgRGB(0xc9, 0x18, 0x47));
	nvgFill(vg);
}


} // namespace rack
