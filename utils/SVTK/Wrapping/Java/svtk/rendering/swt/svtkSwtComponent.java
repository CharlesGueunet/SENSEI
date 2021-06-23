package svtk.rendering.swt;

import org.eclipse.swt.widgets.Composite;

import com.jogamp.opengl.swt.GLCanvas;

import svtk.svtkRenderWindow;
import svtk.rendering.svtkAbstractComponent;

/**
 * Provide SWT based svtk rendering component
 *
 * @author    Joachim Pouderoux - joachim.pouderoux@kitware.com, Kitware SAS 2012
 * @copyright This work was supported by CEA/CESTA
 *            Commissariat a l'Energie Atomique et aux Energies Alternatives,
 *            15 avenue des Sablieres, CS 60001, 33116 Le Barp, France.
 */
public class svtkSwtComponent extends svtkAbstractComponent<GLCanvas> {

  protected svtkInternalSwtComponent uiComponent;
  protected boolean isWindowCreated;

  public svtkSwtComponent(Composite parentComposite) {
    this(new svtkRenderWindow(), parentComposite);
  }

  public svtkSwtComponent(svtkRenderWindow renderWindowToUse, Composite parentComposite) {
    super(renderWindowToUse);
    this.eventForwarder = new svtkSwtInteractorForwarderDecorator(this, this.eventForwarder);
    this.isWindowCreated = true;
    this.uiComponent = new svtkInternalSwtComponent(this, parentComposite);

    renderWindow.AddObserver("StartEvent", this, "startEvent");
    renderWindow.AddObserver("EndEvent", this, "endEvent");
  }

  /**
   * Set the size of the SVTK component
   * @param x width
   * @param y height
   */
  @Override
  public void setSize(int x, int y) {
    x = x < 1 ? 1 : x;
    y = y < 1 ? 1 : y;
    super.setSize(x, y);
    this.uiComponent.setSize(x, y);
    this.uiComponent.redraw();
    this.uiComponent.update();
  }

  /**
   * Render the SVTK component. Should not be called externally.
   * Call update() to refresh the window content.
   */
  @Override
  public void Render() {
    // Make sure we can render
    if (inRenderCall || renderer == null || renderWindow == null) {
      return;
    }

    // Try to render
    try {
      lock.lockInterruptibly();
      inRenderCall = true;
      // Trigger the real render
      renderWindow.Render();
    } catch (InterruptedException e) {
      // Nothing that we can do except skipping execution
    } finally {
      lock.unlock();
      inRenderCall = false;
    }
  }

  /**
   * Redraw the SVTK component
   */
  public void update() {
    this.uiComponent.redraw();
    this.uiComponent.update();
  }

  /**
   * @return the encapsulated SWT component (a GLCanvas instance)
   * @see svtk.rendering.svtkAbstractComponent#getComponent()
   */
  @Override
  public GLCanvas getComponent() {
    return this.uiComponent;
  }

  @Override
  public void Delete() {
    this.lock.lock();
    // We prevent any further rendering
    this.inRenderCall = true;
    this.renderWindow = null;
    super.Delete();
    this.lock.unlock();
  }

  /**
   * @return true if the graphical component has been properly set and
   *         operation can be performed on it.
   */
  public boolean isWindowSet() {
    return this.isWindowCreated;
  }

  /**
   * Just allow class in same package to affect inRenderCall boolean
   *
   * @param value
   */
  protected void updateInRenderCall(boolean value) {
    this.inRenderCall = value;
  }

  /** This method is called by the SVTK JNI code. Do not remove. */
  void startEvent() {
    if (!getComponent().getContext().isCurrent()) {
      getComponent().getContext().makeCurrent();
    }
  }

  /** This method is called by the SVTK JNI code. Do not remove. */
  void endEvent() {
    if (getComponent().getContext().isCurrent()) {
      getComponent().swapBuffers();
      getComponent().getContext().release();
    }
  }
}