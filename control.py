"""
LavaVu python interface: interactive HTML UI controls library
"""
import os
import sys
import time
import json
output = ""

#Register of controls and their actions
actions = []
#Register of windows (viewer instances)
windows = []
#Register of all control objects
allcontrols = []

vertexShader = """
<script id="line-vs" type="x-shader/x-vertex">
precision highp float;
//Line vertex shader
attribute vec3 aVertexPosition;
uniform mat4 uMVMatrix;
uniform mat4 uPMatrix;
uniform vec4 uColour;
varying vec4 vColour;
void main(void)
{
  vec4 mvPosition = uMVMatrix * vec4(aVertexPosition, 1.0);
  gl_Position = uPMatrix * mvPosition;
  vColour = uColour;
}
</script>
"""

fragmentShader = """
<script id="line-fs" type="x-shader/x-fragment">
precision highp float;
varying vec4 vColour;
void main(void)
{
  gl_FragColor = vColour;
}
</script>
"""

#Static HTML location
htmlpath = ""
initialised = False
initcell = ""

def isviewer(target):
    """Return true if target is a viewer"""
    return not hasattr(target, "instance")

def getviewer(target):
    """Return its viewer if target is vis object
    otherwise just return target as if is a viewer"""
    if not isviewer(target):
        return target.instance
    return target

def getproperty(target, propname):
    """Return value of property from target
    if not defined, return the default value for this property
    """
    if propname in target:
        return target[propname]
    else:
        #Get property default
        _lv = target
        _lv = getviewer(target)
        prop = _lv._proplist[propname]
        return prop[0]

def getcontrolvalues():
    """Get the property control values from their targets
    """
    #Build a list of controls to update and their values
    updates = []
    for c in allcontrols:
        if hasattr(c, 'id') and c.elid:
            #print c.elid,c.id
            action = actions[c.id]
            target = action['args'][0]
            if action['type'] == 'PROPERTY':
                propname = action['args'][1]
                #print "  ",c.elid,action
                value = getproperty(target, propname)
                #print "  = ",value
                updates.append([c.elid, value, propname])
    return updates

def export(html):
    if not htmlpath: return
    #Dump all output to control.html
    filename = os.path.join(htmlpath, "control.html")

    #Process actions
    actionjs = '<script type="text/Javascript">\nvar actions = [\n'
    for act in actions:
        #Default placeholder action
        actfunction = ''
        actcmd = None
        if len(act["args"]) == 0:
            #No action
            pass
        elif act["type"] == "PROPERTY":
            #Set a property
            target = act["args"][0]
            # - Globally
            if isviewer(target):
                prop = act["args"][1]
                actfunction = 'select; ' + prop + '=" + value + "'
                if len(act["args"]) > 2:
                    actcmd = act["args"][2]
            #TODO: on an object selector
            # - On an object
            else:
                name = act["args"][0]["name"]
                prop = act["args"][1]
                actfunction = 'select ' + name + '; ' + prop + '=" + value + "'
                if len(act["args"]) > 2:
                    actcmd = act["args"][2]

        #Run a command with a value argument
        elif act["type"] == "COMMAND":
            cmd = act["args"][0]
            if len(act["args"]) > 1:
                actcmd = act["args"][1]
            actfunction = cmd + ' " + value + "'
        #Set a filter range
        elif act["type"] == "FILTER":
            name = act["args"][0]["name"]
            index = act["args"][1]
            prop = act["args"][2]
            cmd = "filtermin" if prop == "minimum" else "filtermax"
            actfunction = 'select ' + name + '; ' + cmd + ' ' + str(index) + ' " + value + "; redraw'
        #Set a colourmap
        elif act["type"] == "COLOURMAP":
            id = act["args"][0].id
            actfunction = 'colourmap ' + str(id) + ' \\"" + value + "\\"' #Reload?

        #Append additional command (eg: reload)
        if actcmd:
          actfunction += ";" + actcmd
        #Add to actions list
        actionjs += '  function(value) {_wi[0].execute("' + actfunction + '", true);},\n'

    #Add init and finish
    actionjs += '  null];\nfunction init() {_wi[0] = new WindowInteractor(0);}\n</script>'
    hfile = open(filename, "w")
    hfile.write("""
    <html>
    <head>
    <meta http-equiv="content-type" content="text/html; charset=ISO-8859-1">
    <script type="text/Javascript" src="control.js"></script>
    <script type="text/Javascript" src="drawbox.js"></script>
    <script type="text/Javascript" src="OK-min.js"></script>
    <script type="text/Javascript" src="gl-matrix-min.js"></script>
    <link rel="stylesheet" type="text/css" href="control.css">""")
    hfile.write(fragmentShader)
    hfile.write(vertexShader)
    hfile.write(actionjs)
    hfile.write('</head>\n<body onload="init();">\n')
    hfile.write(html)
    hfile.write("\n</body>\n</html>")
    hfile.close()
    filename = os.path.join(htmlpath, "control.html")

def initialise():
    global initialised, initcell
    if not htmlpath: return
    try:
        if __IPYTHON__:
            from IPython.display import display,HTML,Javascript
            """ Re-import check
            First check if re-executing the cell init code was inserted from, if so must re-init
            Then sneakily scans the IPython history for "import lavavu" in cell AFTER the one where
            the control interface was last initialised, if it is found, assume we need to initialise again!
            A false positive will add the init code again, which is harmless but bloats the notebook.
            A false negative will cause the interactive viewer widget to not display the webgl bounding box,
            as was the previous behaviour after a re-run without restart.
            """
            ip = get_ipython()
            #Returns a list of executed cells and their content
            history = list(ip.history_manager.get_range())
            #If init was done in cell with exact same code this check is false positive
            #(eg: lv.window())
            if initialised and history[-1][2] != initcell:
                count = 0
                found = False
                #Loop through all the cells in history list
                for cell in history:
                    #Skip cells, until we pass the previous init cell
                    count += 1
                    if count <= initialised: continue;
                    #Each cell is tuple, 3rd element contains line
                    if "import lavavu" in cell[2] or "import glucifer" in cell[2]:
                        #LavaVu has been re-imported, re-init
                        found = True
                        break

                if not found:
                    #Viewer was initialised in an earlier cell
                    return

            #Save cell # and content from history we insert initialisation code at
            initialised = len(history)
            initcell = history[-1][2]

            #Load stylesheet
            css = '<style>\n'
            fo = open(os.path.join(htmlpath, "control.css"), 'r')
            css += fo.read()
            fo.close()
            css += '</style>\n'

            #Load combined javascript libraries
            jslibs = '<script>\n'
            for f in ['gl-matrix-min.js', 'OK-min.js', 'drawbox.js', 'control.js']:
                fpath = os.path.join(htmlpath, f)
                fo = open(fpath, 'r')
                jslibs += fo.read()
                fo.close()
            jslibs += '</script>\n'

            display(HTML(css + fragmentShader + vertexShader + jslibs))
    except (NameError, ImportError):
        pass

def action(id, value):
    #Execute actions from IPython
    global actions
    if len(actions) <= id:
        return "#NoAction " + str(id)

    args = actions[id]["args"]
    act = actions[id]["type"]

    if act == "PROPERTY":
        #Set a property
        if len(args) < 3: return "#args<3"
        target = args[0]
        property = args[1]
        command = args[2]

        target[property] = value
        return command

    elif act == "COMMAND":
        if len(args) < 1: return "#args<1"
        #Run a command with a value argument
        command = args[0]
        return command + " " + str(value) + "\nredraw"

    elif act == "FILTER":
        if len(args) < 3: return "#args<3"
        #Set a filter range
        target = args[0]
        index = args[1]
        property = args[2]

        f = target["filters"]
        f[index][property] = value
        target["filters"] = f
        #return "#" + str(f) + "; redraw"
        #return "redraw"
        return "reload"

    elif act == "COLOURMAP":
        if len(args) < 1: return "#args<1"
        #Set a colourmap
        target = args[0]
        #index = args[1]

        return 'colourmap ' + str(target.id) + ' "' + value + '"'
        """
        map = json.loads(value)
        f = target["colourmaps"]
        maps = target.instance["colourmaps"]
        maps[index] = value
        #print value
        target.instance["colourmaps"] = maps
        return "reload"
        """

    return ""

class HTML(object):
    """A class to output HTML controls
    """
    lastid = 0

    #Parent class for container types
    def __init__(self):
        self.uniqueid()

    def html(self):
        """Return the HTML code"""
        return ''

    def uniqueid(self):
        #Get a unique control identifier
        HTML.lastid += 1
        self.elid = "lvctrl_" + str(HTML.lastid)
        return self.elid

class Container(HTML):
    """A container for a set of controls
    """
    #Parent class for container types
    def __init__(self, viewer):
        self.viewer = viewer
        self.controls = []
        super(Container, self).__init__()

    def add(self, ctrl):
        self.controls.append(ctrl)

    def html(self):
        html = ''
        for i in range(len(self.controls)):
            html += self.controls[i].controls()
        return html

class Window(Container):
    """
    Creates an interaction window with an image of the viewer frame 
    and webgl controller for rotation/translation

    Parameters
    ----------
    align: str
        Set to "left/right" to align viewer window, default is left
    """
    def __init__(self, viewer, align="left"):
        super(Window, self).__init__(viewer)
        self.align = align

    def html(self, wrapper=True, wrapperstyle=""):
        style = 'min-height: 200px; min-width: 200px; background: #ccc; position: relative; display: inline; '
        style += 'float: ' + self.align + ';'
        if wrapper:
            style += ' margin-right: 10px;'
        html = '<div style="' + style + '">\n'
        html += '<img id="imgtarget_---VIEWERID---" draggable=false style="border: 1px solid #aaa;">\n'
        html += '</div>\n'
        #Display any contained controls
        if wrapper:
            html += '<div style="' + wrapperstyle + '" class="lvctrl">\n'
        html += super(Window, self).html()
        if wrapper:
            html += '</div>\n'
        return html

class Panel(Container):
    """Creates a control panel with an interactive viewer window and a set of controls
    By default the controls will be placed to the left with the viewer aligned to the right

    Parameters
    ----------
    showwin: boolean
        Set to False to exclude the interactive window
    """
    def __init__(self, viewer, showwin=True):
        super(Panel, self).__init__(viewer)
        self.win = None
        if showwin:
            self.win = Window(viewer, align="right")

    def html(self):
        if not htmlpath: return
        html = ""
        if self.win: html = self.win.html(wrapper=False)
        #Add control wrapper
        html += '<div style="padding:0px; margin: 0px; position: relative;" class="lvctrl">\n'
        html += super(Panel, self).html()
        html += '</div>\n'
        #if self.win: html += self.win.html(wrapperstyle="float: left; padding:0px; margin: 0px; position: relative;")
        return html

class Tabs(Container):
    """Creates a group of controls with tabs that can be shown or hidden

    Parameters
    ----------
    buttons: boolean
        Display the tab buttons for switching tabs
    """
    def __init__(self, target, buttons=True):
        self.tabs = []
        self.buttons = buttons
        super(Tabs, self).__init__(target)

    def tab(self, label=""):
        """Add a new tab, any controls appending will appear in the new tab
        Parameters
        ----------
        label: str
            Label for the tab, if omitted will be blank
        """
        self.tabs.append(label)

    def add(self, ctrl):
        if not len(self.tabs): self.tab()
        self.controls.append(ctrl)
        ctrl.tab = len(self.tabs)-1

    def html(self):
        if not htmlpath: return
        html = """
        <script>
        function openTab_---ELID---(el, tabName) {
          var i;
          var x = document.getElementsByClassName("---ELID---");
          for (i = 0; i < x.length; i++)
             x[i].style.display = "none";  
          document.getElementById("---ELID---_" + tabName).style.display = "block";  

          tabs = document.getElementsByClassName("tab_---ELID---");
          for (i = 0; i < x.length; i++)
            tabs[i].className = tabs[i].className.replace(" lvseltab", "");
          el.className += " lvseltab";
        }
        </script>
        """
        if self.buttons:
            html += "<div class='lvtabbar'>\n"
            for t in range(len(self.tabs)):
                #Add header items
                classes = 'lvbutton lvctrl tab_---ELID---'
                if t == 0: classes += ' lvseltab'
                html += '<button class="' + classes + '" onclick="openTab_---ELID---(this, this.innerHTML)">---LABEL---</button>\n'
                html = html.replace('---LABEL---', self.tabs[t])
            html += "</div>\n"
        for t in range(len(self.tabs)):
            #Add control wrappers
            style = ''
            if t != 0: style = 'display: none;'
            html += '<div id="---ELID---_---LABEL---" style="' + style + '" class="lvtab lvctrl ---ELID---">\n'
            for ctrl in self.controls:
                if ctrl.tab == t:
                    html += ctrl.controls()
            html += '</div>\n'
            html = html.replace('---LABEL---', self.tabs[t])
        html = html.replace('---ELID---', self.elid)
        return html

class Control(HTML):
    """
    Control object

    Parameters
    ----------
    target: Obj or Viewer
        Add a control to an object to control object properties
        Add a control to a viewer to control global proerties and run commands
    property: str
        Property to modify
    command: str
        Command to run
    value: any type
        Initial value of the controls
    label: str
        Descriptive label for the control
    """

    def __init__(self, target, property=None, command=None, value=None, label=None):
        super(Control, self).__init__()
        self.label = label

        #Get id and add to register
        self.id = len(actions)

        #Can either set a property directly or run a command
        if property:
            #Default action after property set is redraw, can be set to provided
            if command == None:
                command = "redraw"
            actions.append({"type" : "PROPERTY", "args" : [target, property, command]})
            if label == None:
                self.label = property.capitalize()
        elif command:
            actions.append({"type" : "COMMAND", "args" : [command]})
            if label == None:
                self.label = command.capitalize()
        else:
            #Assume derived class will fill out the action
            actions.append({"type" : "PROPERTY", "args" : []})

        if not self.label:
            self.label = ""

        #Get value from target or default if not provided
        if value == None and property != None and target:
            value = getproperty(target, property)
        self.value = value

    def onchange(self):
        return "_wi[---VIEWERID---].do_action(" + str(self.id) + ", this.value, this);"

    def show(self):
        #Show only this control
        html = '<div style="" class="lvctrl">\n'
        html += self.html()
        html += '</div>\n'

    def html(self):
        return self.controls()

    def labelhtml(self):
        #Default label
        html = ""
        if len(self.label):
            html += '<p>' + self.label + ':</p>\n'
        return html

    def controls(self, type='number', attribs={}, onchange=""):
        #Input control
        html =  '<input class="---ELID---" type="' + type + '" '
        for key in attribs:
            html += key + '="' + str(attribs[key]) + '" '
        html += 'value="' + str(self.value) + '" '
        #Onchange event
        onchange += self.onchange()
        html += 'onchange="' + onchange + '" '
        html += '>\n'
        html = html.replace('---ELID---', self.elid)
        return html

class Divider(Control):
    """A divider element
    """

    def controls(self):
        return '<hr style="clear: both;">\n'

class Number(Control):
    """A basic numerical input control
    """

    def controls(self):
        html = self.labelhtml()
        html += super(Number, self).controls()
        return html + '<br>\n'

class Checkbox(Control):
    """A checkbox control for a boolean value
    """
    def labelhtml(self):
        return '' #'<br>\n'

    def controls(self):
        attribs = {}
        if self.value: attribs = {"checked" : "checked"}
        html = "<label>\n"
        html += super(Checkbox, self).controls('checkbox', attribs)
        html += " " + self.label + "</label><br>\n"
        return html

    def onchange(self):
        return "; _wi[---VIEWERID---].do_action(" + str(self.id) + ", this.checked ? 1 : 0, this);"

class Range(Control):
    """A slider control for a range of values

    Parameters
    ----------
    range: list/tuple
        Min/max values for the range
    """
    def __init__(self, target=None, property=None, command=None, value=None, label=None, range=(0.,1.), step=None):
        super(Range, self).__init__(target, property, command, value, label)

        self.range = range
        self.step = step
        if not step:
            #Assume a step size of 1 if range max > 1
            if self.range[1] > 1.0:
                self.step = 1
            else:
                self.step = 0.01

    def controls(self):
        attribs = {"min" : self.range[0], "max" : self.range[1], "step" : self.step}
        html = self.labelhtml()
        html += super(Range, self).controls('number', attribs, onchange='this.nextElementSibling.value=this.value; ')
        html += super(Range, self).controls('range', attribs, onchange='this.previousElementSibling.value=this.value; ')
        return html + '<br>\n'

class Button(Control):
    """A push button control to execute a defined command
    """
    def __init__(self, target, command, label=None):
        super(Button, self).__init__(target, "", command, "", label)

    def onchange(self):
        return "_wi[---VIEWERID---].do_action(" + str(self.id) + ", '', this);"

    def labelhtml(self):
        return ''

    def controls(self):
        html = self.labelhtml()
        html =  '<input class="---ELID---" type="button" value="' + str(self.label) + '" '
        #Onclick event
        html += 'onclick="' + self.onchange() + '" '
        html += '><br>\n'
        html = html.replace('---ELID---', self.elid)
        return html

class Entry(Control):
    """A generic input control for string values
    """
    def controls(self):
        html = self.labelhtml()
        html += '<input class="---ELID---" type="text" value="" onkeypress="if (event.keyCode == 13) { _wi[---VIEWERID---].do_action(---ID---, this.value.trim(), this); };"><br>\n'
        html = html.replace('---ELID---', self.elid)
        return html.replace('---ID---', str(self.id))

class Command(Control):
    """A generic input control for executing command strings
    """
    def __init__(self, *args, **kwargs):
        super(Command, self).__init__(command=" ", label="Command", *args, **kwargs)

    def controls(self):
        html = self.labelhtml()
        html += """
        <input class="---ELID---" type="text" value="" 
        onkeypress="if (event.keyCode == 13) { var cmd=this.value.trim(); 
        _wi[---VIEWERID---].do_action(---ID---, cmd ? cmd : 'repeat', this); this.value=''; };"><br>\n
        """
        html = html.replace('---ELID---', self.elid)
        return html.replace('---ID---', str(self.id))

class List(Control):
    """A list of predefined input values to set properties or run commands

    Parameters
    ----------
    options: list
        List of the available value strings
    """
    def __init__(self, target, options=[], *args, **kwargs):
        self.options = options
        super(List, self).__init__(target, *args, **kwargs)

    def controls(self):
        html = self.labelhtml()
        html += '<select class="---ELID---" id="---ELID---" value="" '
        html += 'onchange="' + self.onchange() + '">\n'
        for opt in self.options:
            #Can be dict {"label" : label, "value" : value, "selected" : True/False}
            #or list [value, label, selected]
            #or just: value
            if isinstance(opt, dict):
                selected = "selected" if opt.selected else ""
                html += '<option value="' + str(opt["value"]) + '" ' + selected + '>' + opt["label"] + '</option>\n'
            elif isinstance(opt, list) or isinstance(opt, tuple):
                selected = "selected" if len(opt) > 2 and opt[2] else ""
                html += '<option value="' + str(opt[0]) + '" ' + selected + '>' + str(opt[1]) + '</option>\n'
            else:
                html += '<option>' + str(opt) + '</option>\n'
        html += '</select><br>\n'
        html = html.replace('---ELID---', self.elid)
        return html

class Colour(Control):
    """A colour picker for setting colour properties
    """
    def __init__(self, *args, **kwargs):
        super(Colour, self).__init__(command="", *args, **kwargs)

    def controls(self):
        html = self.labelhtml()
        html += """
        <div><div class="colourbg checkerboard">
          <div id="---ELID---" class="colour ---ELID---" onclick="
            var col = new Colour();
            var offset = findElementPos(this);
            var el = this;
            var savefn = function(val) {
              var col = new Colour(0);
              col.setHSV(val);
              el.style.backgroundColor = col.html();
              console.log(col.html());
              _wi[---VIEWERID---].do_action(---ID---, col.html(), el);
            }
            el.picker = new ColourPicker(savefn);
            el.picker.pick(col, offset[0], offset[1]);">
            </div>
        </div></div>
        <script>
        var el = document.getElementById("---ELID---");
        //Set the initial colour
        var col = new Colour('---VALUE---');
        el.style.backgroundColor = col.html();
        </script>
        """
        html = html.replace('---VALUE---', str(self.value))
        html = html.replace('---ELID---', self.elid)
        return html.replace('---ID---', str(self.id))

class ColourMap(Control):
    """A colourmap editor
    """
    def __init__(self, target, *args, **kwargs):
        super(ColourMap, self).__init__(target, property="colourmap", command="", *args, **kwargs)
        #Get and save the map id of target object
        self.maps = target.instance.state["colourmaps"]
        if self.value != None and self.value < len(self.maps):
            self.map = self.maps[self.value]
        #Replace action on the control
        actions[self.id] = {"type" : "COLOURMAP", "args" : [target]}
        self.selected = -1;

    def controls(self):
        html = self.labelhtml()
        html += """
        <canvas id="---ELID---" width="512" height="24" class="palette checkerboard">
        </canvas>
        <script>
        var el = document.getElementById("---ELID---");
        el.colourmaps = ---COLOURMAPS---;
        el.selectedIndex = ---SELID---;
        if (!el.gradient) {
          //Create the gradient editor
          el.gradient = new GradientEditor(el, function(obj, id) {
              //Gradient updated
              _wi[---VIEWERID---].do_action(---ID---, obj.palette.toString(), el);

              //Update stored maps list
              if (el.selectedIndex >= 0)
                el.colourmaps[el.selectedIndex] = el.gradient.palette.get().colours;
            }
          , true); //Enable premultiply
          //Load the initial colourmap
          el.gradient.read(---COLOURMAP---);
        }
        </script>
        """
        mapstr = '['
        for m in range(len(self.maps)):
            mapstr += json.dumps(self.maps[m]["colours"])
            if m < len(self.maps)-1: mapstr += ','
        mapstr += ']'
        html = html.replace('---COLOURMAPS---', mapstr)
        html = html.replace('---COLOURMAP---', json.dumps(self.map["colours"]))
        html = html.replace('---SELID---', str(self.selected))
        html = html.replace('---ELID---', self.elid)
        return html.replace('---ID---', str(self.id))

class ColourMapList(List):
    """A colourmap list selector, populated by the default colour maps
    """
    def __init__(self, target, selection=None, *args, **kwargs):
        #Load maps list
        if selection is None:
            selection = target.instance.defaultcolourmaps()
        options = [''] + selection

        super(ColourMapList, self).__init__(target, options=options, command="reload", property="colourmap", *args, **kwargs)

        #Replace action on the control
        actions[self.id] = {"type" : "COLOURMAP", "args" : [target]}

class ColourMaps(List):
    """A colourmap list selector, populated by the available colour maps,
    combined with a colourmap editor for the selected colour map
    """
    def __init__(self, target, *args, **kwargs):
        #Load maps list
        self.maps = target.instance.state["colourmaps"]
        options = [[-1, "None"]]
        for m in range(len(self.maps)):
            options.append([m, self.maps[m]["name"]])
        #Mark selected
        sel = target["colourmap"]
        if sel is None: sel = -1
        options[sel+1].append(True)

        self.gradient = ColourMap(target)
        self.gradient.selected = sel #gradient editor needs to know selection index
        self.gradient.label = "" #Clear label

        super(ColourMaps, self).__init__(target, options=options, command="reload", property="colourmap", *args, **kwargs)

    def onchange(self):
        #Find the saved palette entry and load it
        script = """
        var el = document.getElementById('---PALLID---'); 
        var sel = document.getElementById('---ELID---');
        if (sel.selectedIndex > 0) {
            el.gradient.read(el.colourmaps[sel.selectedIndex-1]);
            el.selectedIndex = sel.selectedIndex-1;
        }
        """
        return script + super(ColourMaps, self).onchange()

    def controls(self):
        html = super(ColourMaps, self).controls() + self.gradient.controls()
        html = html.replace('---PALLID---', str(self.gradient.elid))
        return html

class TimeStepper(Range):
    """A time step selection range control with up/down buttons
    """
    def __init__(self, viewer, *args, **kwargs):
        #Acts as a command setter with some additional controls
        super(TimeStepper, self).__init__(target=viewer, label="Timestep", command="timestep", *args, **kwargs)

        self.timesteps = viewer.timesteps()
        self.range = (self.timesteps[0], self.timesteps[-1])
        self.step = 1
        #Calculate step gap
        if len(self.timesteps) > 1:
            self.step = self.timesteps[1] - self.timesteps[0]
        self.value = 0

    def controls(self):
        html = Range.controls(self)
        html += '<input type="button" style="width: 50px;" onclick="var el = this.previousElementSibling.previousElementSibling; el.stepDown(); el.onchange()" value="&larr;" />'
        html += '<input type="button" style="width: 50px;" onclick="var el = this.previousElementSibling.previousElementSibling.previousElementSibling; el.stepUp(); el.onchange()" value="&rarr;" />'
        return html

class DualRange(Control):
    """A set of two range slider controls for adjusting a minimum and maximum range

    Parameters
    ----------
    range: list/tuple
        Min/max values for the range
    """
    def __init__(self, target, properties, values, label, range=(0.,1.), step=None):
        self.label = label

        self.ctrlmin = Range(target=target, property=properties[0], step=step, value=values[0], range=range, label="")
        self.ctrlmax = Range(target=target, property=properties[1], step=step, value=values[1], range=range, label="")

    def controls(self):
        return self.labelhtml() + self.ctrlmin.controls() + self.ctrlmax.controls()

class Filter(Control):
    """A set of two range slider controls for adjusting a minimum and maximum filter range

    Parameters
    ----------
    range: list/tuple
        Min/max values for the filter range
    """
    def __init__(self, target, filteridx, label=None, range=None, step=None):
        self.label = label
        self.filter = target["filters"][filteridx]

        #Default label - data set name
        if label == None:
            self.label = self.filter['by'].capitalize()

        #Get the default range limits from the matching data source
        self.data = target["data"][self.filter['by']]
        if not range:
            #self.range = (self.filter["minimum"], self.filter["maximum"])
            if self.filter["map"]:
                range = (0.,1.)
            else:
                range = (self.data["minimum"], self.data["maximum"])

        self.ctrlmin = Range(step=step, range=range, value=self.filter["minimum"])
        self.ctrlmax = Range(step=step, range=range, value=self.filter["maximum"])

        #Replace actions on the controls
        actions[self.ctrlmin.id] = {"type" : "FILTER", "args" : [target, filteridx, "minimum"]}
        actions[self.ctrlmax.id] = {"type" : "FILTER", "args" : [target, filteridx, "maximum"]}

    def controls(self):
        return self.labelhtml() + self.ctrlmin.controls() + self.ctrlmax.controls()

class ObjectList(Control):
    """A set checkbox controls for controlling visibility of all visualisation objects
    """
    def __init__(self, viewer, *args, **kwargs):
        super(ObjectList, self).__init__(target=viewer, label="Objects", *args, **kwargs)
        self.objctrls = []
        for obj in viewer.objects.list:
            self.objctrls.append(Checkbox(obj, "visible", label=obj["name"])) 

    def controls(self):
        html = self.labelhtml()
        for ctrl in self.objctrls:
            html += ctrl.controls()
        return html

class ObjectSelect(List):
    """A list selector of all visualisation objects that can be used to
    choose the target of a set of controls

    Parameters
    ----------
    objects: list
        A list of objects to display, by default all available objects are added
    """
    def __init__(self, viewer, objects=None, *args, **kwargs):
        if not isviewer(viewer):
            print "Can't add ObjectSelect control to an Object, must add to Viewer"
            return
        self.instance = viewer
        if objects is None:
            objects = viewer.objects.list
        
        #Load maps list
        self.object = 0 #Default to no selection
        options = [(0, "None")]
        for o in range(len(objects)):
            obj = objects[o]
            options += [(o+1, obj["name"])]

        super(ObjectSelect, self).__init__(target=self, label="Objects", options=options, property="object", *args, **kwargs)
        #Holds a control factory so controls can be added with this as a target
        self.control = ControlFactory(self)

    def onchange(self):
        #Update the control values on change
        return super(ObjectSelect, self).onchange() + "; getAndUpdateControlValues();"

    def __contains__(self, key):
        #print "CONTAINS",key
        #print "OBJECT == ",self.object,(key in self.instance.objects.list[self.object-1])
        return key == "object" or self.object > 0 and key in self.instance.objects.list[self.object-1]

    def __getitem__(self, key):
        #print "GETITEM",key
        if key == "object":
            return self.object
        elif self.object > 0:
            return self.instance.objects.list[self.object-1][key]
        return None

    def __setitem__(self, key, value):
        #print "SETITEM",key,value
        if key == "object":
            self.object = value
            #Copy object id
            self.id = self.instance.objects.list[self.object-1].id
            #Update controls
            #self.instance.control.update()
        elif self.object > 0:
            self.instance.objects.list[self.object-1][key] = value

class ObjectTabs(ObjectSelect):
    """Object selction with control tabs for each object"""
    def __init__(self, *args, **kwargs):
        super(ObjectTabs, self).__init__(target=self, label="Objects", options=options, property="object", *args, **kwargs)
    #Add predefined controls?
    #

class ControlFactory(object):
    """
    Create and manage sets of controls for interaction with a Viewer or Object
    Controls can run commands or change properties
    """
    #Creates a control factory used to generate controls for a specified target
    def __init__(self, target):
        self._target = target
        self.clear()
        self.interactor = False
        self.output = ""

        #Save types of all control/containers
        def all_subclasses(cls):
            return cls.__subclasses__() + [g for s in cls.__subclasses__() for g in all_subclasses(s)]

        #Control contructor shortcut methods
        #(allows constructing controls directly from the factory object)
        #Use a closure to define a new method to call constructor and add to controls
        def addmethod(constr):
            def method(*args, **kwargs):
                #Return the new control and add it to the list
                newctrl = constr(self._target, *args, **kwargs)
                self.add(newctrl)
                return newctrl
            return method

        self._control_types = all_subclasses(Control)
        self._container_types = all_subclasses(Container)
        for constr in self._control_types + self._container_types:
            key = constr.__name__
            method = addmethod(constr)
            #Set docstring (+ Control docs)
            if constr in self._control_types:
                method.__doc__ = constr.__doc__ + Control.__doc__
            else:
                method.__doc__ = constr.__doc__
            self.__setattr__(key, method)

    def add(self, ctrl):
        """
        Add a control
        """
        if type(ctrl) in self._container_types:
            #Save new container, further controls will be added to it
            self._container = ctrl
        elif self._container:
            #Add to existing container
            self._container.add(ctrl)
        else:
            #Add to global list
            self._controls.append(ctrl)

        #Add to viewer instance list too if not already being added
        if not isviewer(self._target):
            self._target.instance.control.add(ctrl)
        else:
            #Add to master list - not cleared after display
            allcontrols.append(ctrl)

    def show(self, fallback=None):
        """
        Displays all added controls including viewer if any

        fallback: function
            A function which is called in place of the viewer display when run outside IPython
        """
        #Show all controls in container
        if not htmlpath: return

        #Creates an interactor to connect javascript/html controls to IPython and viewer
        #if no viewer Window() created, it will be a windowless interactor
        viewerid = len(windows)
        if isviewer(self._target):
            try:
                #Find viewer id
                viewerid = windows.index(self._target)
            except (ValueError):
                #Append the current viewer ref
                windows.append(self._target)
                #Use viewer instance just added
                viewerid = len(windows)-1

        #Generate the HTML
        html = ""
        chtml = ""
        for c in self._controls:
            chtml += c.html()
        if len(chtml):
            html = '<div style="" class="lvctrl">\n' + chtml + '</div>\n'
        if self._container:
            html += self._container.html()

        #Set viewer id
        html = html.replace('---VIEWERID---', str(viewerid))

        #Display HTML inline or export
        self.output += html
        try:
            if __IPYTHON__:
                from IPython.display import display,HTML,Javascript
                #Interaction requires some additional js/css/webgl
                initialise()

                #Output the controls
                display(HTML(html))

                #Create WindowInteractor instance
                js = '_wi[' + str(viewerid) + '] = new WindowInteractor(' + str(viewerid) + ');'
                display(Javascript(js))

            else:
                export(self.output)
                if callable(fallback): fallback(self._target)
        except (NameError, ImportError):
            export(self.output)
            if callable(fallback): fallback()

        #Auto-clear after show?
        self.clear()

    def redisplay(self):
        """Update the active viewer image if any
        Applies changes made in python to the viewer and forces a redisplay
        """
        try:
            #Find viewer id
            viewerid = windows.index(self._target)
            if __IPYTHON__:
                from IPython.display import display,Javascript
                js = '_wi[' + str(viewerid) + '].redisplay(' + str(viewerid) + ');'
                display(Javascript(js))

        except (NameError, ImportError, ValueError):
            pass

    def update(self):
        """Update the control values from current viewer data
        Applies changes made in python to the UI controls
        """
        try:
            #Build a list of controls to update and their values
            #pass it to javascript function to update the page
            updates = getcontrolvalues()
            if __IPYTHON__:
                from IPython.display import display,Javascript
                jso = json.dumps(updates)
                js = "updateControlValues(" + jso + ");"
                display(Javascript(js))
        except (NameError, ImportError, ValueError) as e:
            print str(e)
            pass
        
    def clear(self):
        self._controls = []
        self._container = None

